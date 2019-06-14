################################################################################
#
# Set up env using spack build tpls on Linux RHEL platforms for ATMD builds of
# Trilinos
#
# This source script gets the settings from the ATDM_CONFIG_BUILD_NAME var.
#
################################################################################

#
# Deal with compiler versions
#

if [ "$ATDM_CONFIG_COMPILER" == "DEFAULT" ] ; then
  export ATDM_CONFIG_COMPILER=GNU-7.2.0
elif [[ "$ATDM_CONFIG_COMPILER" == "GNU"* ]]; then
  if [[ "$ATDM_CONFIG_COMPILER" == "GNU" ]] ; then
    export ATDM_CONFIG_COMPILER=GNU-7.2.0
  elif [[ "$ATDM_CONFIG_COMPILER" != "GNU-7.2.0" ]] ; then
    echo
    echo "***"
    echo "*** ERROR: GNU COMPILER=$ATDM_CONFIG_COMPILER is not supported!"
    echo "*** Only GNU compilers supported on this system are:"
    echo "***   gnu (defaults to gnu-7.2.0)"
    echo "***   gnu-7.2.0"
    echo "***"
    return
  fi
else
  echo
  echo "***"
  echo "*** ERROR: COMPILER=$ATDM_CONFIG_COMPILER is not supported!"
  echo "***"
  return
fi

#
# Allow KOKKOS_ARCH to be set but unset it if DEFAULT
#

if [ "$ATDM_CONFIG_KOKKOS_ARCH" == "DEFAULT" ] ; then
  unset ATDM_CONFIG_KOKKOS_ARCH
else
  echo
  echo "***"
  echo "*** ERROR: Specifying KOKKOS_ARCH is not supported on RHEL ATDM builds"
  echo "*** remove '$ATDM_CONFIG_KOKKOS_ARCH' from ATDM_CONFIG_BUILD_NAME=$ATDM_CONFIG_BUILD_NAME"
  echo "***"
  return
fi

echo "Using SPACK RHEL compiler stack $ATDM_CONFIG_COMPILER to build $ATDM_CONFIG_BUILD_TYPE code with Kokkos node type $ATDM_CONFIG_NODE_TYPE"

export ATDM_CONFIG_ENABLE_SPARC_SETTINGS=ON
export ATDM_CONFIG_USE_NINJA=ON

# Get ATDM_CONFIG_NUM_CORES_ON_MACHINE for this machine
source $ATDM_SCRIPT_DIR/utils/get_num_cores_on_machine.sh

if [ "$ATDM_CONFIG_NUM_CORES_ON_MACHINE" -gt "20" ] ; then
  export ATDM_CONFIG_MAX_NUM_CORES_TO_USE=20
  # NOTE: We get links crashing if we try to use to many processes.  ToDo: We
  # should limit the number of processes that ninja uses to link instead of
  # reducing the overrall parallel build level like this.
else
  export ATDM_CONFIG_MAX_NUM_CORES_TO_USE=$ATDM_CONFIG_NUM_CORES_ON_MACHINE
fi

export ATDM_CONFIG_BUILD_COUNT=$ATDM_CONFIG_MAX_NUM_CORES_TO_USE
# NOTE: Use as many build processes and there are cores by default.

module purge
module load gcc-7.2.0/spack-cmake/3.13.4
module load gcc-7.2.0/spack-git/2.20.1
module load gcc-7.2.0/spack-ninja-fortran/1.7.2.gaad58

if [[ "$ATDM_CONFIG_NODE_TYPE" == "OPENMP" ]] ; then
  export ATDM_CONFIG_CTEST_PARALLEL_LEVEL=$(($ATDM_CONFIG_MAX_NUM_CORES_TO_USE/2))
  export OMP_NUM_THREADS=2
  export OMP_PROC_BIND=false
  unset OMP_PLACES
  # NOTE: With hyper-threading enabled, you can run as many threads as there
  # are cores and with 2 OpenMP threads per MPI process, the means you can run
  # as many MPI processes as there are cores on the machine with 2 OpenMP
  # threads.  But we want to be conservative and instead run with half that
  # many to be safe and avoid time-outs.
else
  export ATDM_CONFIG_CTEST_PARALLEL_LEVEL=$(($ATDM_CONFIG_MAX_NUM_CORES_TO_USE/2))
  export OMP_NUM_THREADS=1
  export OMP_PROC_BIND=false
  unset OMP_PLACES
  # NOTE: NOTE: When running in serial, the second hyperthread can't seem to
  # run a sperate MPI process and if you try to run with as many they you get
  # a bunch of failures that say "libgomp: Thread creation failed: Resource
  # temporarily unavailable".  So we can only run with as many MPI processes
  # as there are cores on the machine.  But we want to be conservative and
  # instead run with half that many to be safe and avoid time-outs.
fi

if [ "$ATDM_CONFIG_COMPILER" == "GNU-7.2.0" ]; then

  SPACK_GCC_COMPILER_TO_LOAD=`module avail 2>&1 | grep spack-gcc/7.2.0`
  module load ${SPACK_GCC_COMPILER_TO_LOAD}
  export OMPI_CXX=`which g++`
  export OMPI_CC=`which gcc`
  export OMPI_FC=`which gfortran`

  module load gcc-7.2.0/spack-netlib-lapack/3.8.0
  export BLAS_ROOT=$NETLIB_LAPACK_ROOT
  export LAPACK_ROOT=$NETLIB_LAPACK_ROOT

  module load gcc-7.2.0/spack-binutils/2.31.1
  module load gcc-7.2.0/spack-gettext/0.19.8.1 # for binutils
  module load gcc-7.2.0/spack-libiconv/1.15 # for gettext 
  module load gcc-7.2.0/spack-openmpi/1.10.1
  module load gcc-7.2.0/spack-boost/1.59.0
  module load gcc-7.2.0/spack-netcdf/4.4.1
  module load gcc-7.2.0/spack-parallel-netcdf/1.11.0
  module load gcc-7.2.0/spack-superlu/4.3
  module load gcc-7.2.0/spack-hdf5/1.8.21
  module load gcc-7.2.0/spack-zlib/1.2.11
  module load gcc-7.2.0/spack-metis/5.1.0
  module load gcc-7.2.0/spack-parmetis/4.0.3
  module load gcc-7.2.0/spack-cgns/snl-atdm
  module load gcc-7.2.0/spack-superlu-dist/6.1.0

else
  echo
  echo "***"
  echo "*** ERROR: COMPILER=$ATDM_CONFIG_COMPILER is not supported on this system!"
  echo "***"
  return
fi

if [[ "${ATDM_CONFIG_SHARED_LIBS}" == "ON" ]] ; then
  ATDM_CONFIG_TPL_LIB_EXT=so
else
  ATDM_CONFIG_TPL_LIB_EXT=a
fi

export ATDM_CONFIG_USE_HWLOC=OFF
export HWLOC_LIBS=-lhwloc

export ATDM_CONFIG_LAPACK_LIBS="-L${LAPACK_ROOT}/lib64;-llapack"
export ATDM_CONFIG_BLAS_LIBS="-L${BLAS_ROOT}/lib64;-lblas"

export ATDM_CONFIG_HDF5_LIBS="-L${HDF5_ROOT}/lib;${HDF5_ROOT}/lib/libhdf5_hl.a;${HDF5_ROOT}/lib/libhdf5.a;${ZLIB_ROOT}/lib/libz.a;-ldl"
#echo ATDM_CONFIG_HDF5_LIBS=$ATDM_CONFIG_HDF5_LIBS

export PNETCDF_ROOT=${PARALLEL_NETCDF_ROOT}

export ATDM_CONFIG_METIS_LIBS="${METIS_ROOT}/lib/libmetis.so"
export ATDM_CONFIG_PARMETIS_LIBS="${METIS_ROOT}/lib/libmetis.so;${PARMETIS_ROOT}/lib/libparmetis.so"
#export ATDM_CONFIG_PNETCDF_LIBS=
export ATDM_CONFIG_CGNS_LIBS="${CGNS_ROOT}/lib/libcgns.a;-L${HDF5_ROOT}/lib;${HDF5_ROOT}/lib/libhdf5_hl.a;${HDF5_ROOT}/lib/libhdf5.a;-lz;-ldl"

#export METIS_LIBRARY_DIRS=${METIS_ROOT}/lib
#export METIS_INCLUDE_DIRS=${METIS_ROOT}/include

export ATDM_CONFIG_NETCDF_LIBS="-L${BOOST_ROOT}/lib;-L${NETCDF_ROOT}/lib;-L${NETCDF_ROOT}/lib;-L${PNETCDF_ROOT}/lib;-L${HDF5_ROOT}/lib;${BOOST_ROOT}/lib/libboost_program_options.${ATDM_CONFIG_TPL_LIB_EXT};${BOOST_ROOT}/lib/libboost_system.${ATDM_CONFIG_TPL_LIB_EXT};${NETCDF_ROOT}/lib/libnetcdf.a;${PARALLEL_NETCDF_ROOT}/lib/libpnetcdf.a;${HDF5_ROOT}/lib/libhdf5_hl.a;${HDF5_ROOT}/lib/libhdf5.a;-lz;-ldl;-lcurl"
# NOTE: SEMS does not provide the correct *.so files for NetCDF so we can't
# use them in a shared lib build :-(

export ATDM_CONFIG_BINUTILS_LIBS="${BINUTILS_ROOT}/lib/libbfd.so;${BINUTILS_ROOT}/lib64/libiberty.a;${GETTEXT_ROOT}/lib/libintl.a;${LIBICONV_ROOT}/lib/libiconv.so"
# NOTE: Above, we have to explicitly set the libs to use libbdf.so instead of
# libbdf.a because the former works and the latter does not and TriBITS is set
# up to only find static libs by default!
unset BINUTILS_ROOT
# NOTE: Above, you have to unset BINUTILS_ROOT or the CMake configure of
# Trilinos dies in the compiler check.  (We should investigate this more at
# some point.)

# SuperLUDist
if [[ "${ATDM_CONFIG_SUPERLUDIST_INCLUDE_DIRS}" == "" ]] ; then
  # Set the default which is correct for all of the new TPL builds
  export ATDM_CONFIG_SUPERLUDIST_INCLUDE_DIRS=${SUPERLU_DIST_ROOT}/include
  export ATDM_CONFIG_SUPERLUDIST_LIBS=${SUPERLU_DIST_ROOT}/lib/libsuperlu_dist.so
fi


# Set MPI wrappers
export MPICC=`which mpicc`
export MPICXX=`which mpicxx`
export MPIF90=`which mpif90`

export ATDM_CONFIG_MPI_PRE_FLAGS="--bind-to;none"

export ATDM_CONFIG_COMPLETED_ENV_SETUP=TRUE
