/**
   C++ implementation of the MMCS algorithm
   Copyright Vera-Licona Research Group (C) 2015
   Author: Andrew Gainer-Dewar, Ph.D. <andrew.gainer.dewar@gmail.com>
**/

#include <string>
#include <vector>
#include <boost/dynamic_bitset.hpp>
#include <iostream>
#include <cassert>
#include <stdexcept>
#include <algorithm>
#include <fstream>
#include <sstream>

#include <boost/program_options.hpp>

#include "concurrentqueue.h"
#include "mmcs.h"

typedef boost::dynamic_bitset<> bitset;
typedef std::vector<bitset> bsvector;
typedef bitset::size_type bindex;
typedef bsvector::size_type bsvindex;

namespace po = boost::program_options;

int fork_at_depth = 0; // Tuneable parameter

moodycamel::ConcurrentQueue<bitset> HittingSets;

int main(int argc, char * argv[]) {
    // SET UP ARGUMENTS
    po::options_description desc("Options");
    desc.add_options()
        ("input", po::value<std::string>()->required(), "Input hypergraph file")
        ("output", po::value<std::string>()->default_value("out.dat"), "Output transversals file");

    po::positional_options_description p;
    p.add("input", -1);
    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), vm);

    if (vm.count("help") or argc == 1) {
        std::cout << desc << std::endl;
        return 1;
    };

    po::notify(vm);

    // Process input file
    std::string input_file(vm["input"].as<std::string>());
    bsvector H = hypergraph_from_file(input_file);
    bindex n_edges = H.size();
    bsvindex n_verts = H[0].size();

    // SET UP INTERNAL VARIABLES
    // Candidate hitting set
    bitset S (n_verts);
    S.reset(); // Initially empty

    // Eligible vertices
    bitset CAND (n_verts);
    CAND.set(); // Initially full


    // Which edges each vertex is critical for
    bsvector crit (n_verts, bitset(n_edges));
    for (auto & critset: crit) {
        critset.reset(); // Each one initially empty
    }

    // Which edges are uncovered
    bitset uncov (n_edges);
    uncov.set(); // Initially full

    // RUN ALGORITHM
    {
#pragma omp parallel shared(H)
#pragma omp single
        extend_or_confirm_set(H, S, CAND, crit, uncov);
    }

    // Print results
    std::string output_file(vm["output"].as<std::string>());
    write_results_to_file(output_file);
}

bsvector hypergraph_from_file(const std::string & hypergraph_file) {
    // Set up file reader
    std::ifstream hypergraph_filestream(hypergraph_file);
    if (!hypergraph_filestream.good()) {
        std::stringstream errorMessage;
        errorMessage << "Could not open hypergraph file " << hypergraph_file << " for reading.";
        throw std::runtime_error(errorMessage.str());
    }

    // Set up intermediate variables
    std::vector<std::vector<bindex>> edges;
    bindex max_vertex = 0;
    bsvindex n_edges = 0;

    // Read the file line by line
    for (std::string line; std::getline(hypergraph_filestream, line); ) {
        // Each line is an edge
        std::istringstream linestream(line);
        ++n_edges;
        std::vector<bindex> edge;
        for (bindex v; linestream >> v; ) {
            // Each word of the line is a vertex
            edge.push_back(v);
            max_vertex = std::max(max_vertex, v);
        }
        edges.push_back(edge);
    }

    // Set up the hypergraph as a vector of bitsets
    bsvector H (n_edges, bitset(max_vertex+1));
    for (bsvindex i = 0; i < edges.size(); ++i) {
        for (auto& v: edges[i]) {
            H[i][v] = true;
        }
    }
    return H;
}

void write_results_to_file(const std::string & output_file) {
    // Set up file writer
    std::ofstream output_filestream(output_file);
    if (!output_filestream.good()) {
        std::stringstream errorMessage;
        errorMessage << "Could not open output file " << output_file << " for writing.";
        throw std::runtime_error(errorMessage.str());
    }

    bitset result;
    while (HittingSets.try_dequeue(result)) {
        bindex i = result.find_first();
        while (i != bitset::npos) {
            output_filestream << i << " ";
            i = result.find_next(i);
        }
        output_filestream << std::endl;
    }
}

void extend_or_confirm_set(bsvector H,
                           bitset S,
                           bitset CAND,
                           bsvector crit,
                           bitset uncov,
                           int current_recursion_depth) {
    // Handle recursion depth
    bool do_fork;
    if (current_recursion_depth <= 0) {
        current_recursion_depth = fork_at_depth;
        do_fork = true;
    } else {
        --current_recursion_depth;
        do_fork = false;
    }

    // if uncov is empty, S is a hitting set
    if (uncov.none()) {
        HittingSets.enqueue(S);
        return;
    }

    // If CAND is empty, S cannot be extended, so we're done
    if (CAND.none()) {
        return;
    }

    // Otherwise, get an uncovered edge and remove its elements from CAND
    // TODO: Implement the optimization of Murakami and Uno
    bitset e = H[uncov.find_first()]; // Just use the first set in uncov
    bitset C = CAND & e; // intersection
    CAND = CAND & (~e); // difference

    // Test all the vertices in C
    auto vert_index = C.find_first();
    while (vert_index != bitset::npos) {
#pragma omp task untied if (do_fork) shared(H)
        test_vertex(H, S, CAND, crit, uncov, vert_index, current_recursion_depth);
        CAND[vert_index] = true;
        vert_index = C.find_next(vert_index);
    }
#pragma omp taskwait
}

void test_vertex(bsvector H,
                 bitset S,
                 bitset CAND,
                 bsvector crit,
                 bitset uncov,
                 bindex vertex_to_test,
                 int current_recursion_depth) {
    // Update uncov and crit by iterating over edges containing the vertex
    for (bsvindex edge_index = 0; edge_index < H.size(); ++edge_index) {
        // If the vertex is in this edge, proceed
        if (H[edge_index][vertex_to_test]) {
            // Remove e from all crit[v]'s
            for (bindex v = 0; v < crit.size(); ++v) {
                crit[v][edge_index] = false;
            }

            // If this edge was uncovered, it is no longer, but v is now critical for it
            if (uncov[edge_index] == true) {
                uncov[edge_index] = false;
                crit[vertex_to_test][edge_index] = true;
            }
        }
    }

    // Construct the new candidate hitting set
    bitset newS = S;
    newS[vertex_to_test] = true;

    // Test the minimality condition on newS
    auto v = newS.find_first();
    while (v != bitset::npos) {
        if (crit[v].none()) {
            return;
        }
        v = newS.find_next(v);
    }

    // If we made it this far, minimality holds, so we process newS
    if (uncov.none()) {
        HittingSets.enqueue(newS);
    } else {
        extend_or_confirm_set(H, newS, CAND, crit, uncov, current_recursion_depth);
    }
}