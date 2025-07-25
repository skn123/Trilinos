/*
 * Copyright(C) 1999-2024 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS).  Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 *
 * See packages/seacas/LICENSE for details
 */
/*****************************************************************************
 *
 * exopen - ex_open
 *
 * entry conditions -
 *   input parameters:
 *       char*   path                    exodus filename path
 *       int     mode                    access mode w/r
 *
 * exit conditions -
 *       int     exoid                   exodus file id
 *       int*    comp_ws                 computer word size
 *       int*    io_ws                   storage word size
 *       float*  version                 EXODUS interface version number
 *
 * revision history -
 *
 *
 *****************************************************************************/

#include "exodusII.h"     // for ex_err, etc
#include "exodusII_int.h" // for EX_FATAL, etc
#include <stdlib.h>
/*!
\ingroup Utilities

\note The ex_open_int() is an internal function called by
ex_open(). The user should call ex_open() and not ex_open_int().

The function ex_open() opens an existing exodus file and returns
an ID that can subsequently be used to refer to the file, the word
size of the floating point values stored in the file, and the version
of the exodus database (returned as a ``float'', regardless of the
compute or I/O word size). Multiple files may be ``open'' simultaneously.

\return In case of an error, ex_open() returns a negative
number. Possible causes of errors include:
  -  The specified file does not exist.
  -  The mode specified is something other than the predefined constant
\fparam{#EX_READ} or \fparam{#EX_WRITE}.
  -  Database version is earlier than 2.0.

\param path The file name of the exodus file. This can be given as either an
            absolute path name (from the root of the file system) or a relative
            path name (from the current directory).

\param mode Access mode. Use one of the following predefined constants:
        -  \fparam{EX_READ} To open the file just for reading.
        -  \fparam{EX_WRITE} To open the file for writing and reading.

\param[in,out] comp_ws The word size in bytes (0, 4 or 8) of the floating point
variables
               used in the application program. If 0 (zero) is passed, the
               default size of floating point values for the machine will be
               used and returned in this variable. WARNING: all exodus functions
               requiring reals must be passed reals declared with this passed
               in or returned compute word size (4 or 8).

\param[in,out] io_ws The word size in bytes (0, 4 or 8) of the floating
                    point data as they are stored in the exodus file. If the
                    word size does not match the word size of data stored in
                    the file, a fatal error is returned. If this argument is
                    0, the word size of the floating point data already
                    stored in the file is returned.

\param[out] version  Returned exodus database version number. Note that this is always a float,
                     never a double.

\param[in] run_version Internally generated to verify library compatibility.

The following opens an exodus file named \file{test.exo} for read
only, using default settings for compute and I/O word sizes:

~~~{.c}
int CPU_word_size,IO_word_size, exoid;
float version;

CPU_word_size = sizeof(float);   \co{float or double}
IO_word_size = 0;                \co{use what is stored in file}

\comment{open exodus files}
exoid = ex_open ("test.exo",     \co{filename path}
                 EX_READ,        \co{access mode = READ}
                 &CPU_word_size, \co{CPU word size}
                 &IO_word_size,  \co{IO word size}
                 &version);      \co{ExodusII library version}
~~~
 */

/* NOTE: Do *not* call `ex_open_int()` directly.  The public API
 *       function name is `ex_open()` which is a wrapper that calls
 *       `ex_open_int` with an additional argument to make sure
 *       library and include file are consistent
 */
int ex_open_int(const char *path, int mode, int *comp_ws, int *io_ws, float *version,
                int run_version)
{
  int     exoid         = -1;
  int     status        = 0;
  nc_type att_type      = NC_NAT;
  int     old_fill      = 0;
  int     file_wordsize = 0;
  int     int64_status  = 0;
  int     nc_mode       = 0;

  char errmsg[MAX_ERR_LENGTH];

  EX_FUNC_ENTER();
  *version = 0.0f;

  /* set error handling mode to no messages, non-fatal errors */
  ex_opts(exoptval); /* call required to set ncopts first time through */

  exi_check_version(run_version);

  if ((mode & EX_READ) && (mode & EX_WRITE)) {
    snprintf(errmsg, MAX_ERR_LENGTH, "ERROR: Cannot specify both EX_READ and EX_WRITE");
    ex_err(__func__, errmsg, EX_BADFILEMODE);
    EX_FUNC_LEAVE(EX_FATAL);
  }

  if (!path || strlen(path) == 0) {
    snprintf(errmsg, MAX_ERR_LENGTH, "ERROR: Filename is not specified.");
    ex_err(__func__, errmsg, EX_BADFILEMODE);
    EX_FUNC_LEAVE(EX_FATAL);
  }

  char *canon_path = exi_canonicalize_filename(path);

  /* Verify that this file is not already open for read or write...
     In theory, should be ok for the file to be open multiple times
     for read, but bad things can happen if being read and written
     at the same time...
  */
  if (exi_check_multiple_open(canon_path, mode, __func__)) {
    free(canon_path);
    EX_FUNC_LEAVE(EX_FATAL);
  }

  /* The EX_READ mode is the default if EX_WRITE is not specified... */
  if (!(mode & EX_WRITE)) { /* READ ONLY */
    nc_mode = NC_NOWRITE | NC_SHARE;

#if NC_HAS_DISKLESS
    if (mode & EX_DISKLESS) {
      nc_mode |= NC_DISKLESS;
    }
#endif

    if ((status = nc_open(canon_path, nc_mode, &exoid)) != EX_NOERR) {
      /* NOTE: netCDF returns an id of -1 on an error - but no error code! */
      /* It is possible that the user is trying to open a netcdf4
         file, but the netcdf4 capabilities aren't available in the
         netcdf linked to this library. Note that we can't just use a
         compile-time define since we could be using a shareable
         netcdf library, so the netcdf4 capabilities aren't known
         until runtime...

         Later versions of netcdf-4.X have a function that can be
         queried to determine whether the library being used was
         compiled with --enable-netcdf4, but not everyone is using that
         version yet, so we may have to do some guessing...

         At this time, query the beginning of the file and see if it
         is an HDF-5 file and if it is assume that the open failure
         is due to the netcdf library not enabling netcdf4 features unless
         we have the define that shows it is enabled, then assume other error...
      */
      int type = 0;
      ex_opts(EX_VERBOSE);

      exi_check_file_type(canon_path, &type);

      if (type == 0) {
        /* Error message printed at lower level */
      }
      else if (type == 5) {
#if NC_HAS_HDF5
        snprintf(errmsg, MAX_ERR_LENGTH,
                 "EXODUS: ERROR: Attempting to open the netcdf-4 "
                 "file:\n\t'%s'\n\tfailed. The netcdf library supports "
                 "netcdf-4 so there must be a filesystem or some other "
                 "issue.\n",
                 canon_path);
        ex_err(__func__, errmsg, status);
        free(canon_path);
        EX_FUNC_LEAVE(EX_FATAL);
#else
        /* This is an hdf5 (netcdf4) file. If NC_HAS_HDF5 is not defined,
           then we either don't have hdf5 support in this netcdf version,
           OR this is an older netcdf version that doesn't provide that define.

           In either case, we don't have enough information, so we
           assume that the netcdf doesn't have netcdf4 capabilities
           enabled.  Tell the user...
        */
        snprintf(errmsg, MAX_ERR_LENGTH,
                 "EXODUS: ERROR: Attempting to open the netcdf-4 "
                 "file:\n\t'%s'.\n\tEither the netcdf library does not "
                 "support netcdf-4 or there is a filesystem or some "
                 "other issue.\n",
                 canon_path);
        ex_err(__func__, errmsg, status);
        free(canon_path);
        EX_FUNC_LEAVE(EX_FATAL);
#endif
      }
      else if (type == 4) {
#if NC_HAS_CDF5
        snprintf(errmsg, MAX_ERR_LENGTH,
                 "EXODUS: ERROR: Attempting to open the CDF5 "
                 "file:\n\t'%s'\n\tfailed. The netcdf library supports "
                 "CDF5-type files so there must be a filesystem or some other "
                 "issue \n",
                 canon_path);
        ex_err(__func__, errmsg, status);
        free(canon_path);
        EX_FUNC_LEAVE(EX_FATAL);
#else
        /* This is an cdf5 (64BIT_DATA) file. If NC_64BIT_DATA is not defined,
           then we either don't have cdf5 support in this netcdf version,
           OR this is an older netcdf version that doesn't provide that define.

           In either case, we don't have enough information, so we
           assume that the netcdf doesn't have cdf5 capabilities
           enabled.  Tell the user...
        */
        snprintf(errmsg, MAX_ERR_LENGTH,
                 "EXODUS: ERROR: Attempting to open the CDF5 "
                 "file:\n\t'%s'.\n\tEither the netcdf library does not "
                 "support CDF5 or there is a filesystem or some "
                 "other issue \n",
                 canon_path);
        ex_err(__func__, errmsg, status);
        free(canon_path);
        EX_FUNC_LEAVE(EX_FATAL);

#endif
      }
      else if (type == 1) {
        /* Possibly an issue with an older file created by a Java-based NetCDF library which wrote
         * corrupted data which is now being checked by newer releases of the NetCDF library...
         * SEE: https://github.com/Unidata/netcdf-c/issues/1115
         */
        snprintf(errmsg, MAX_ERR_LENGTH,
                 "ERROR: failed to open '%s' of type %d for reading.\n"
                 "\t\tIf this is an old file, it is possible that it has some internal corruption\n"
                 "\t\tthat is now being checked by recent versions of the NetCDF library.\n"
                 "\t\tTo fix, you can find an older version of `nccopy` (prior to 4.6.0)\n"
                 "\t\tthen try `nccopy bad_file.g fixed_file.g`.",
                 canon_path, type);
        ex_err(__func__, errmsg, status);
        free(canon_path);
        EX_FUNC_LEAVE(EX_FATAL);
      }
      snprintf(errmsg, MAX_ERR_LENGTH,
               "ERROR: failed to open %s of type %d for reading.\n\t\tThe "
               "file does not exist, or there is a permission or file "
               "format issue.",
               canon_path, type);
      ex_err(__func__, errmsg, status);
      free(canon_path);
      EX_FUNC_LEAVE(EX_FATAL);
    }
  }
  else { /* (mode & EX_WRITE) READ/WRITE */
    nc_mode = NC_WRITE | NC_SHARE;
#if NC_HAS_DISKLESS
    if (mode & EX_DISKLESS) {
      nc_mode |= NC_DISKLESS;
#if defined NC_PERSIST
      nc_mode |= NC_PERSIST;
#endif
    }
#endif
    if ((status = nc_open(canon_path, nc_mode, &exoid)) != EX_NOERR) {
      /* NOTE: netCDF returns an id of -1 on an error - but no error code! */
      snprintf(errmsg, MAX_ERR_LENGTH,
               "ERROR: failed to open %s for read/write.\n\tEither the file "
               "does not exist,\n\tor there is a permission or file format "
               "issue.",
               canon_path);
      ex_err(__func__, errmsg, status);
      free(canon_path);
      EX_FUNC_LEAVE(EX_FATAL);
    }

    /* turn off automatic filling of netCDF variables */
    if ((status = nc_set_fill(exoid, NC_NOFILL, &old_fill)) != EX_NOERR) {
      snprintf(errmsg, MAX_ERR_LENGTH, "ERROR: failed to set nofill mode in file id %d named %s",
               exoid, canon_path);
      ex_err_fn(exoid, __func__, errmsg, status);
      free(canon_path);
      EX_FUNC_LEAVE(EX_FATAL);
    }

    size_t att_len      = 0;
    int    stat_att     = nc_inq_att(exoid, NC_GLOBAL, ATT_MAX_NAME_LENGTH, &att_type, &att_len);
    int    dim_str_name = 0;
    int    stat_dim     = nc_inq_dimid(exoid, DIM_STR_NAME, &dim_str_name);
    if (stat_att != EX_NOERR || stat_dim != EX_NOERR) {
      /* This must still be nc_redef */
      if ((status = nc_redef(exoid)) != EX_NOERR) {
        snprintf(errmsg, MAX_ERR_LENGTH,
                 "ERROR: failed to place file id %d named %s into define mode", exoid, canon_path);
        ex_err_fn(exoid, __func__, errmsg, status);
        free(canon_path);
        EX_FUNC_LEAVE(EX_FATAL);
      }

      if (stat_att != EX_NOERR) {
        int max_so_far = 32;
        if ((status = nc_put_att_int(exoid, NC_GLOBAL, ATT_MAX_NAME_LENGTH, NC_INT, 1,
                                     &max_so_far)) != EX_NOERR) {
          snprintf(errmsg, MAX_ERR_LENGTH,
                   "ERROR: failed to add maximum_name_length attribute in file id %d", exoid);
          ex_err_fn(exoid, __func__, errmsg, status);
          free(canon_path);
          EX_FUNC_LEAVE(EX_FATAL);
        }
      }

      /* If the DIM_STR_NAME variable does not exist on the database, we need to
       * add it now. */
      if (stat_dim != EX_NOERR) {
        /* Not found; set to default value of 32+1. */
        int max_name = exi_default_max_name_length < 32 ? 32 : exi_default_max_name_length;
        if ((status = nc_def_dim(exoid, DIM_STR_NAME, max_name + 1, &dim_str_name)) != EX_NOERR) {
          snprintf(errmsg, MAX_ERR_LENGTH,
                   "ERROR: failed to define string name dimension in file id %d named %s", exoid,
                   canon_path);
          ex_err_fn(exoid, __func__, errmsg, status);
          free(canon_path);
          EX_FUNC_LEAVE(EX_FATAL);
        }
      }
      if ((status = nc_enddef(exoid)) != EX_NOERR) {
        snprintf(errmsg, MAX_ERR_LENGTH, "ERROR: failed to exit define mode in file id %d", exoid);
        ex_err_fn(exoid, __func__, errmsg, status);
        free(canon_path);
        EX_FUNC_LEAVE(EX_FATAL);
      }
    }
  }

  /* determine version of EXODUS file, and the word size of
   * floating point and integer values stored in the file
   */

  if ((status = nc_get_att_float(exoid, NC_GLOBAL, ATT_VERSION, version)) != EX_NOERR) {
    snprintf(errmsg, MAX_ERR_LENGTH, "ERROR: failed to get database version for file id: %d",
             exoid);
    ex_err_fn(exoid, __func__, errmsg, status);
    free(canon_path);
    EX_FUNC_LEAVE(EX_FATAL);
  }

  /* check ExodusII file version - old version 1.x files are not supported */
  if (*version < 2.0) {
    snprintf(errmsg, MAX_ERR_LENGTH, "ERROR: Unsupported file version %.2f in file id: %d",
             *version, exoid);
    ex_err_fn(exoid, __func__, errmsg, EX_BADPARAM);
    free(canon_path);
    EX_FUNC_LEAVE(EX_FATAL);
  }

  if (nc_get_att_int(exoid, NC_GLOBAL, ATT_FLT_WORDSIZE, &file_wordsize) != EX_NOERR) {
    /* try old (prior to db version 2.02) attribute name */
    if ((status = nc_get_att_int(exoid, NC_GLOBAL, ATT_FLT_WORDSIZE_BLANK, &file_wordsize)) !=
        EX_NOERR) {
      snprintf(errmsg, MAX_ERR_LENGTH, "ERROR: failed to get file wordsize from file id: %d",
               exoid);
      ex_err_fn(exoid, __func__, errmsg, status);
      free(canon_path);
      EX_FUNC_LEAVE(EX_FATAL);
    }
  }

  /* See if int64 status attribute exists and if so, what data is stored as
   * int64
   * Older files don't have the attribute, so it is not an error if it is
   * missing
   */
  if (nc_get_att_int(exoid, NC_GLOBAL, ATT_INT64_STATUS, &int64_status) != EX_NOERR) {
    int64_status = 0; /* Just in case it gets munged by a failed get_att_int call */
  }

  /* Merge in API int64 status flags as specified by caller of function... */
  int64_status |= (mode & EX_ALL_INT64_API);

  /* Verify that there is not an existing file_item struct for this
     exoid This could happen (and has) when application calls
     ex_open(), but then closes file using nc_close() and then reopens
     file.  NetCDF will possibly reuse the exoid which results in
     internal corruption in exodus data structures since exodus does
     not know that file was closed and possibly new file opened for
     this exoid
  */
  if (exi_find_file_item(exoid) != NULL) {
    snprintf(errmsg, MAX_ERR_LENGTH,
             "ERROR: There is an existing file already using the file "
             "id %d which was also assigned to file %s.\n\tWas "
             "nc_close() called instead of ex_close() on an open Exodus "
             "file?\n",
             exoid, canon_path);
    ex_err_fn(exoid, __func__, errmsg, EX_BADFILEID);
    nc_close(exoid);
    free(canon_path);
    EX_FUNC_LEAVE(EX_FATAL);
  }

  /* initialize floating point and integer size conversion. */
  if (exi_conv_init(exoid, comp_ws, io_ws, file_wordsize, int64_status, false, false, false,
                    mode & EX_WRITE) != EX_NOERR) {
    snprintf(errmsg, MAX_ERR_LENGTH,
             "ERROR: failed to initialize conversion routines in file id %d named %s", exoid,
             canon_path);
    ex_err_fn(exoid, __func__, errmsg, EX_LASTERR);
    free(canon_path);
    EX_FUNC_LEAVE(EX_FATAL);
  }

  free(canon_path);
  EX_FUNC_LEAVE(exoid);
}
