/**
 * @file test_hdf5_mpi.c
 * @brief HDF5 + MPI-IO FAPL smoke test for dftracer interception.
 *
 * Opens an HDF5 file via the MPI-IO FAPL driver (H5Pset_fapl_mpio) so that
 * HDF5 internally routes all file I/O through MPI_File_open / MPI_File_write_at
 * / MPI_File_read_at.  This produces both HDF5 and MPI-IO events in the trace
 * even when run as a single process (MPI_COMM_WORLD with size=1).
 *
 * Requires HDF5 built with parallel support (-DHDF5_ENABLE_PARALLEL=ON).
 * The CMakeLists.txt guards this target with HDF5_IS_PARALLEL so it is only
 * built when the found HDF5 has MPIO support.
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <dftracer/dftracer.h>
#include <hdf5.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char* argv[]) {
  MPI_Init(&argc, &argv);

  int rank = 0, size = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  int init_dftracer = 0;
  if (argc > 2 && strcmp(argv[2], "1") == 0) {
    DFTRACER_C_INIT(NULL, NULL, NULL);
    init_dftracer = 1;
  }

  const char* data_dir = (argc > 1) ? argv[1] : "/tmp";
  char filename[1024];
  snprintf(filename, sizeof(filename), "%s/test_hdf5_mpi_fapl.h5", data_dir);

  /* --- open file via MPI-IO FAPL so HDF5 uses MPI_File_* internally --- */
  hid_t fapl = H5Pcreate(H5P_FILE_ACCESS);
  if (fapl < 0) {
    fprintf(stderr, "H5Pcreate failed\n");
    MPI_Abort(MPI_COMM_WORLD, 1);
  }
  if (H5Pset_fapl_mpio(fapl, MPI_COMM_WORLD, MPI_INFO_NULL) < 0) {
    fprintf(stderr, "H5Pset_fapl_mpio failed\n");
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  hid_t file = H5Fcreate(filename, H5F_ACC_TRUNC, H5P_DEFAULT, fapl);
  H5Pclose(fapl);
  if (file < 0) {
    fprintf(stderr, "H5Fcreate failed\n");
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  /* --- dataset --- */
  hsize_t dims[1] = {8};
  hid_t space = H5Screate_simple(1, dims, NULL);
  hid_t dset = H5Dcreate2(file, "data", H5T_NATIVE_INT, space, H5P_DEFAULT,
                          H5P_DEFAULT, H5P_DEFAULT);
  if (dset < 0) {
    fprintf(stderr, "H5Dcreate2 failed\n");
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  /* --- collective transfer property list --- */
  hid_t dxpl = H5Pcreate(H5P_DATASET_XFER);
  H5Pset_dxpl_mpio(dxpl, H5FD_MPIO_COLLECTIVE);

  int wbuf[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  int rbuf[8] = {0};

  if (H5Dwrite(dset, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, dxpl, wbuf) < 0) {
    fprintf(stderr, "H5Dwrite failed\n");
    MPI_Abort(MPI_COMM_WORLD, 1);
  }
  if (H5Dread(dset, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, dxpl, rbuf) < 0) {
    fprintf(stderr, "H5Dread failed\n");
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  H5Pclose(dxpl);
  H5Dclose(dset);
  H5Sclose(space);
  H5Fclose(file);

  int sum = 0;
  for (int i = 0; i < 8; i++) sum += rbuf[i];

  if (init_dftracer) {
    DFTRACER_C_FINI();
  }

  MPI_Finalize();

  if (rank == 0)
    printf("HDF5+MPI FAPL smoke test completed successfully (sum=%d)\n", sum);
  return (sum == 36) ? 0 : 1;
}
