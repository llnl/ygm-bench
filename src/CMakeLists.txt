# Copyright 2019-2021 Lawrence Livermore National Security, LLC and other YGM
# Project Developers. See the top-level COPYRIGHT file for details.
#
# SPDX-License-Identifier: MIT

setup_ygm_target(around_the_world_ygm)
setup_ygm_target(agups_ygm)
setup_ygm_target(histo_ygm)
setup_ygm_target(barrier_bench)
setup_ygm_target(cc_ygm)
setup_ygm_target(rmat_example)

setup_ygm_target(around_the_world_mpi)

# Does not currently compile. Will revisit.
#setup_krowkee_target(embed_ygm)
