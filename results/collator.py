#!/usr/bin/env python
# coding: utf-8

# MHS algorithm benchmark output collator
# Copyright Vera-Licona Research Group (C) 2015
# Author: Andrew Gainer-Dewar, Ph.D. <andrew.gainer.dewar@gmail.com>

import argparse
import json
import pandas
import numpy
import os
from matplotlib import pyplot as plt
from collections import defaultdict

def parse_results_algname(algname_str):
    orig_name = algname_str

    # Process the name into its components
    parts = orig_name.split("-")

    # The algorithm name always comes first
    stripped_name = orig_name[0]

    # The thread and cutoff specifications, if any, come later
    threads = 1
    cutoff = 0

    for specpart in parts[1:]:
        if specpart[0] == 't':
            threads = int(specpart[1:])
        if specpart[0] == 'c':
            cutoff = int(specpart[1:])

    # Build the return dict
    result = {
        "alg_name": stripped_name,
        "threads": threads,
        "cutoff": cutoff
    }

    return result

def algorithm_result_frame(alg, runtimes):
    # Build a DataFrame with the results of a given algorithm in the given
    # results record

    # Find the instances of this algorithm in the results file
    alg_instances = filter(lambda algname: algname.split('-')[0] == alg, runtimes)

    # For each one, find the runtimes
    results = defaultdict(dict)
    for alg_instance in alg_instances:
        alg_instance_runtimes = runtimes[alg_instance]
        median_runtime = numpy.median(alg_instance_runtimes)

        alg_instance_specs = parse_results_algname(alg_instance)
        threads = alg_instance_specs["threads"]
        cutoff = alg_instance_specs["cutoff"]
        results[cutoff][threads] = median_runtime

    # TODO: Can we annotate this DataFrame to explain the axis labels?
    result_frame = pandas.DataFrame(results)
    return result_frame

def problem_results_panel(results_json):
    runtimes = results_json["runtimes"]

    # Get the list of algorithm names
    algs = set(alg["algName"] for alg in results_json["algs"])

    # For each algorithm, build a DataFrame with its results
    results = dict()
    for alg in algs:
        results[alg] = algorithm_result_frame(alg, runtimes)

    result_panel = pandas.Panel(results)
    return result_panel

def panel4d_from_filenames(alg_filenames):
    results = dict()

    for input_filename in alg_filenames:
        with open(input_filename) as input_file:
            problem_results_json = json.load(input_file)

        problem_results = problem_results_panel(problem_results_json)

        problem_shortname = os.path.splitext(os.path.basename(input_filename))[0]
        results[problem_shortname] = problem_results

    result_panel = pandas.Panel4D(results)
    return result_panel

def main():
    # Set up argument processing
    parser = argparse.ArgumentParser(description="MHS algorithm benchmark runner")

    # Add arguments
    parser.add_argument("output_file_basename", help="Base name for output files")
    parser.add_argument("input_files", help="Input file(s) to collate", nargs="+")

    # Process the arguments
    args = parser.parse_args()

    # Process the files
    results = panel4d_from_filenames(args.input_files)

    # Generate the results and write the plots to files
    # For now, we only use the t=1 data
    for cutoff_size in [0, 5, 7, 10]:
        # Construct filename
        output_file_name = "{0}.c{1}.pdf".format(args.output_file_basename, cutoff_size)

        # Make sure the output file does not exist
        if os.path.exists(output_file_name):
            raise ValueError("Output file {0} already exists!".format(output_file_name))

        # Generate the plot
        try:
            data = results.ix[:,:,1,cutoff_size].T.dropna(axis='columns', how='all') # Extract the data we want
        except KeyError:
            # If the requested cutoff_size is not available, just move on to the next one
            print "Skipping cutoff_size {0}".format(cutoff_size)
            continue
        data.plot(logy = True, ylim = [0, 3600])
        plt.savefig(output_file_name)

if __name__ == "__main__":
    main()

### Emacs configuration
# Local Variables:
# mode: python
# End
