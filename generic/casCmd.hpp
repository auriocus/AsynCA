#ifndef CASCMD_H
#define CASCMD_H

#include <casdef.h>
#undef INLINE /* conflicting definition from Tcl and EPICS */
#include <tcl.h>
#include "casExport.h"

#include <fdmgr.h>

#include "map.h"

struct GetRequestEvent {
	Tcl_Event ev;
	casPV *PV;
};

extern "C" {
	static Tcl_ThreadCreateProc EpicsEventLoop;
}

// server instance
class AsynServer {
public:
    AsynServer (Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]);
    ~AsynServer();

	int test(Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]);
	int test2(Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]);

	Tcl_ThreadId mainid;
};

#define TCLCLASSDECLARE(CLASS) \
extern "C" { \
	static int CLASS ## CreateCmd(ClientData cdata, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]); \
	static int CLASS ## InstanceCmd(ClientData cdata, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]); \
	static void CLASS ## DeleteCmd(ClientData cdata); \
\
}

#define ENTRY(CLASS, FUN) { #FUN, &CLASS::FUN },

#define TCLCLASSSUBCMDS(CLASS, ...) \
	static struct CLASS ## SubCmdEntry { \
		const char *name; \
		int (CLASS::*fun) (Tcl_Interp *, int, Tcl_Obj *const[]); \
	} CLASS ## SubCmdTable [] = { \
		MAPARG(ENTRY, CLASS, __VA_ARGS__) \
		{ NULL, NULL } \
	};



TCLCLASSDECLARE(AsynServer)
TCLCLASSSUBCMDS(AsynServer, test, test2);

// server instance
class AsynPV {
public:
    AsynPV () { }
    ~AsynPV() { }

	Tcl_ThreadId mainid;
};


//TCLCLASSDECLARE(AsynPV)
#endif
