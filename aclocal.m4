#
# Include the TEA standard macro set
#

builtin(include,tclconfig/tcl.m4)

#
# Add here whatever m4 macros you want to define for your package
#
builtin(include,tclconfig/ax_epics_base.m4)
builtin(include,tclconfig/m4_ax_cxx_compile_stdcxx.m4)
