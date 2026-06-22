/*
 * MPI implementation detection via preprocessor macros defined in <mpi.h>.
 * More specific implementations are checked first because several are layered
 * on top of MPICH and therefore also define MPICH_VERSION.
 *
 * Outputs two lines to stdout:
 *   IMPL=<NAME>
 *   VERSION=<major.minor.patch>
 *
 * Known IMPL values: OPENMPI, INTELMPI, CRAYMPICH, MVAPICH, MPICH, UNKNOWN
 */
#include <mpi.h>
#include <stdio.h>

int main(void) {
#if defined(OMPI_MAJOR_VERSION)
    /* Open MPI: OMPI_MAJOR_VERSION / OMPI_MINOR_VERSION / OMPI_RELEASE_VERSION */
    printf("IMPL=OPENMPI\nVERSION=%d.%d.%d\n",
           OMPI_MAJOR_VERSION, OMPI_MINOR_VERSION, OMPI_RELEASE_VERSION);
#elif defined(I_MPI_VERSION)
    /* Intel MPI (MPICH-based) — check before generic MPICH */
    printf("IMPL=INTELMPI\nVERSION=%s\n", I_MPI_VERSION);
#elif defined(CRAY_MPICH_VERSION)
    /* Cray MPICH — check before generic MPICH */
    printf("IMPL=CRAYMPICH\nVERSION=%s\n", CRAY_MPICH_VERSION);
#elif defined(MVAPICH2_VERSION)
    /* MVAPICH2 (MPICH-based) — check before generic MPICH */
    printf("IMPL=MVAPICH\nVERSION=%s\n", MVAPICH2_VERSION);
#elif defined(MPICH_VERSION)
    /* Vanilla MPICH */
    printf("IMPL=MPICH\nVERSION=%s\n", MPICH_VERSION);
#else
    printf("IMPL=UNKNOWN\nVERSION=0.0.0\n");
#endif
    return 0;
}
