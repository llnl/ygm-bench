#! /usr/bin/env python3

import argparse
import subprocess
import math
import shutil
import random
import os

def parse_arguments():
    parser = argparse.ArgumentParser()

    # Arguments for job launcher
    parser.add_argument("--min-nodes", type=int, help="Minimum number of nodes", required=True)
    parser.add_argument("--max-nodes", type=int, help="Maximum number of nodes", required=True)
    parser.add_argument("--tmp-dir", help="Temporary directory to launch batch jobs from")
    parser.add_argument("-o", "--output-dir", help="Output directory")
    parser.add_argument("-A", "--account", help="Bank to use with Slurm")
    parser.add_argument("--use-lsf", action="store_true", help="Use LSF scheduler instead of Slurm")

    # Experiment arguments
    parser.add_argument("-s", "--table-scale-per-node", type=int, help="log_2 of table size per node for use in histo and agups experiments (default=20)", default=20)
    parser.add_argument("-g", "--cc-graph-scale-per-node", type=int, help="Logarithmic graph scale per node for connected components \
            experiments (overridden by --cc-rmat-graph-scale-per-node and --cc-linked-list-graph-scale-per-node)", default=20)
    parser.add_argument("--cc-rmat-graph-scale-per-node", type=int, help="Logarithmic graph scale per node for connected components \
            RMAT experiments")
    parser.add_argument("--cc-linked-list-graph-scale-per-node", type=int, help="Logarithmic graph scale per node for connected components \
            linked list experiments")
    parser.add_argument("-v", "--krowkee-vertex-scale-per-node", type=int, help="log_2 of vertex count per node for \
            krowkee experiments", default=20)

    return parser.parse_known_args()

def run_experiments(args, passthrough_args):
    if args.tmp_dir:
        tmp_experiment_dir = args.tmp_dir + "/" + str(random.randrange((1 << 32)-1))
        shutil.copytree("../scripts", tmp_experiment_dir + "/scripts")
        shutil.copytree("../build/src", tmp_experiment_dir + "/build/src")
        os.chdir(tmp_experiment_dir + "/scripts")

    num_nodes = args.min_nodes

    while num_nodes <= args.max_nodes:
        if args.output_dir:
            output_filename = args.output_dir + "/ygm_bench_N" + str(num_nodes)
        else:
            output_filename = "ygm_bench_N" + str(num_nodes)

        table_scale = args.table_scale_per_node + int(math.log2(num_nodes))
        cc_rmat_scale = args.cc_graph_scale_per_node + int(math.log2(num_nodes))
        cc_linked_list_scale = args.cc_graph_scale_per_node + int(math.log2(num_nodes))
        if args.cc_rmat_graph_scale_per_node:
            cc_rmat_scale = args.cc_rmat_graph_scale_per_node + int(math.log2(num_nodes))
        if args.cc_linked_list_graph_scale_per_node:
            cc_linked_list_scale = args.cc_linked_list_graph_scale_per_node + int(math.log2(num_nodes))
        if args.krowkee_vertex_scale_per_node:
            krowkee_vertex_scale = args.krowkee_vertex_scale_per_node + int(math.log2(num_nodes))

        run_experiments_options = ""

        run_experiments_options += " --table-scale " + str(table_scale) 
        run_experiments_options += " --cc-rmat-graph-scale " + str(cc_rmat_scale)
        run_experiments_options += " --cc-linked-list-graph-scale " + str(cc_linked_list_scale)
        run_experiments_options += " --krowkee-log-vertex-count " + str(krowkee_vertex_scale)

        if (args.use_lsf):
            bsub_script="#!/usr/bin/env bash"

            if args.account:
                bsub_script += "\n#BSUB -G " + args.account

            bsub_script += "\n#BSUB -W 360"
            bsub_script += "\n#BSUB -o " + output_filename
            bsub_script += "\n#BSUB -nnodes " + str(num_nodes)
            bsub_script += "\n#BSUB -J ygm-bench"
            bsub_script += "\n./run_experiments.py --use-lsf " + run_experiments_options + ' ' + ' '.join(passthrough_args)
            print(bsub_script)
            proc = subprocess.run("bsub", input=bsub_script, text=True, \
                    env=dict(os.environ, SLURM_JOB_NUM_NODES=str(num_nodes)))
        else:
            sbatch_script="#!/usr/bin/env bash"

            if args.account:
                sbatch_script += "\n#SBATCH -A " + args.account

            sbatch_script += "\n#SBATCH -p pbatch"
            sbatch_script += "\n#SBATCH -t 1:00:00"
            sbatch_script += "\n#SBATCH -o " + output_filename
            sbatch_script += "\n#SBATCH -N " + str(num_nodes)
            sbatch_script += "\n#SBATCH -J ygm-bench"
            sbatch_script += "\n./run_experiments.py " + run_experiments_options + ' ' + ' '.join(passthrough_args)
            print(sbatch_script)
            proc = subprocess.run("sbatch", input=sbatch_script, text=True)

        num_nodes *= 2

def main():
    args, passthrough_args = parse_arguments()

    run_experiments(args, passthrough_args)

if __name__ == "__main__":
    main()
