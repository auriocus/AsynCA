# ===========================================================================
#       http://www.gnu.org/software/autoconf-archive/ax_epics_base.html
# ===========================================================================
#
# SYNOPSIS
#
#   AX_EPICS_BASE([MINIMUM-VERSION])
#
# DESCRIPTION
#
#   Test for the EPICS base libraries of a particular version (or newer)
#
#   If no path to the installed epics library is given, the macro 
#   evaluates $EPICS_BASE environment variable and searches
#   under /usr/local/epics/base.
#
#   This macro sets:
#
#     HAVE_EPICS
#     EPICS_BASE
#     EPICS_HOST_ARCH
#     EPICS_OS_CLASS
#     EPICS_CMPLR_CLASS
#
# LICENSE
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved. This file is offered as-is, without any
#   warranty.

#serial 1

DEFAULT_EPICS_VERSION=3.14.1

AC_DEFUN([AX_EPICS_BASE],

[AC_ARG_WITH([epics-base],
    [AS_HELP_STRING([--with-epics-base=EPICS_BASE],
        [Use EPICS from the specified location])
    ],
    [
        if test -z "$withval"; then 
            AC_MSG_ERROR(--with-epics-base requires directory name)
        else
            ac_epics_base_path="$withval"
        fi
    ],
    [ac_epics_base_path=""])

    if test -z "$ac_epics_base_path"; then
        if test -z "$EPICS_BASE"; then
            ac_epics_base_path="/usr/local/epics/base"
        else
            ac_epics_base_path=$EPICS_BASE
        fi
    fi
    AC_MSG_CHECKING(for EPICS base directory $ac_epics_base_path)
    if ! test -d "$ac_epics_base_path"; then
        AC_MSG_ERROR($ac_epics_base_path is not a valid directory path)
    fi

    epics_version_h="$ac_epics_base_path/include/epicsVersion.h"
    if ! test -f "$epics_version_h"; then
        AC_MSG_RESULT([no])
        AC_MSG_ERROR(could not find valid EPICS base installation in $ac_epics_base_path: no version header file $epics_version_h)
    fi
    AC_MSG_RESULT([yes])

    # define EPICS_BASE
    EPICS_BASE=$ac_epics_base_path

    # determine requested version
    epics_version_req=ifelse([$1], , $DEFAULT_EPICS_VERSION, $1)
    AC_MSG_CHECKING(for EPICS base >= $epics_version_req)
	IFS=. read epics_version_req_major  epics_version_req_minor epics_version_req_sub_minor epics_version_req_patch <<< "$epics_version_req"
    if test "x$epics_version_req_sub_minor" = "x" ; then
        epics_version_req_sub_minor="0"
        epics_version_req_patch="0"
    else
        if test "x$epics_version_req_patch" = "x" ; then
            epics_version_req_patch="0"
        fi
    fi

    # get epics version info
    epics_version_major=`grep EPICS_VERSION $epics_version_h | head -1 | awk '{print $NF}' | sed $'s/\r//'`
    epics_version_minor=`grep EPICS_REVISION $epics_version_h | head -1 | awk '{print $NF}' | sed $'s/\r//'`
    epics_version_sub_minor=`grep EPICS_MODIFICATION $epics_version_h | head -1 | awk '{print $NF}' | sed $'s/\r//'`
    epics_version_patch=`grep EPICS_PATCH_LEVEL $epics_version_h | head -1 | awk '{print $NF}' | sed $'s/\r//'`

    # test version
    want_epics_version=`expr $epics_version_req_major \* 1000000 \+  $epics_version_req_minor \* 10000 \+ $epics_version_req_sub_minor \* 100 \+ $epics_version_req_patch`
    have_epics_version=`expr $epics_version_major \* 1000000 \+  $epics_version_minor \* 10000 \+ $epics_version_sub_minor \* 100 \+ $epics_version_patch`
    if ! test $have_epics_version -ge $want_epics_version; then
        AC_MSG_RESULT([no])
        AC_MSG_ERROR("EPICS base installation in $ac_epics_base_path is too old (required: $want_epics_version, found: $have_epics_version)")
    fi
    AC_MSG_RESULT([yes])

    # determine epics host arch
    if test -z "$EPICS_HOST_ARCH"; then
        EPICS_HOST_ARCH=`$ac_epics_base_path/startup/EpicsHostArch`
    fi
    if test -z "$EPICS_HOST_ARCH"; then
        AC_MSG_ERROR(could not determine EPICS host architecture)
    fi
	
	EPICS_LIBDIR="$EPICS_BASE/lib/$EPICS_HOST_ARCH"
	
	dnl Example of default-enabled feature
	AC_ARG_ENABLE([static-epics],
		AS_HELP_STRING([--enable-static-epics], [Enable static linking of EPICS libraries (default: on)]), 
		[static_epics=$enableval], [static_epics=yes])

    # test basic libraries
    # need to determine OS class first
    epics_host_arch_main=`echo $EPICS_HOST_ARCH | cut -f1 -d'-'`
    EPICS_OS_CLASS=`grep OS_CLASS $EPICS_BASE/configure/os/CONFIG.*Common.${epics_host_arch_main}* | head -1 | awk '{print $NF}'`
    # need to determine CMPLR_CLASS, which is *hard*, so
    # TODO: remove this hardcoded setting with the correct one from the EPICS configure directory
    EPICS_CMPLR_CLASS=$(ls -1 $EPICS_BASE/include/compiler/)

	# find additional libraries to link 
	#ARCH_DEP_LDLIBS=`awk '/^\s*ARCH_DEP_LDLIBS/ {$[1]=""; $[2]=""; print}' $EPICS_BASE/config/CONFIG.Host.$EPICS_OS_CLASS`

    AC_MSG_CHECKING(for usable EPICS base libraries for $EPICS_OS_CLASS OS ($EPICS_CMPLR_CLASS compiler) and host architecture $EPICS_HOST_ARCH)

    succeeded=no
    CPPFLAGS_SAVED="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $EPICS_CPPFLAGS -I$EPICS_BASE/include -I$EPICS_BASE/include/os/$EPICS_OS_CLASS -I$EPICS_BASE/include/compiler/$EPICS_CMPLR_CLASS"
    export CPPFLAGS

    LDFLAGS_SAVED="$LDFLAGS"
    LDFLAGS="$LDFLAGS $EPICS_LDFLAGS"
    LDFLAGS="$LDFLAGS -L$EPICS_BASE/lib/$EPICS_HOST_ARCH"
    export LDFLAGS

	echo "The libraries are $LIBS"
	if test "x$static_epics" != "xno"; then
		SLIBS="$EPICS_LIBDIR/libca.a $EPICS_LIBDIR/libCom.a $EPICS_LIBDIR/libcas.a $EPICS_LIBDIR/libgdd.a"
		export LIBS="$SLIBS $SLIBS $LIBS"
	else
		export LIBS="-lca -lCom -lcas -lgdd $LIBS"
	fi

    export LIBS

    AC_LANG_PUSH([C++])
        AC_LINK_IFELSE([AC_LANG_PROGRAM(
            [[#include <cadef.h>]],
            [[ca_context_create(ca_enable_preemptive_callback);]])
        ],[succeeded=yes],[succeeded=no])
    AC_LANG_POP([C++])

    if test "$succeeded" != "yes" ; then
        AC_MSG_RESULT([no])
        AC_MSG_ERROR(could not compile and link EPICS test code: check your EPICS base installation)
    else
        AC_MSG_RESULT([yes])
        AC_DEFINE(HAVE_EPICS, , [define if the EPICS base is available])
        AC_SUBST(EPICS_BASE)
        AC_SUBST(EPICS_HOST_ARCH)
        AC_SUBST(EPICS_OS_CLASS)
        AC_SUBST(EPICS_CMPLR_CLASS)
    fi

    #CPPFLAGS="$CPPFLAGS_SAVED"
    #LDFLAGS="$LDFLAGS_SAVED"

])
