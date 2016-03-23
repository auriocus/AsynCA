/*
 * caCmd.c --
 */

//#include "caCmd.h"
#include <cadef.h>
#undef INLINE /* conflicting definition from Tcl and EPICS */
#include <tcl.h>
#include <string.h>
#include <stdlib.h>

/* Copy of this, because it is not exported (but uses only public functionality) */
/*
 *----------------------------------------------------------------
 * Macro used by the Tcl core to clean out an object's internal
 * representation. Does not actually reset the rep's bytes. The ANSI C
 * "prototype" for this macro is:
 *
 * MODULE_SCOPE void	TclFreeIntRep(Tcl_Obj *objPtr);
 *----------------------------------------------------------------
 */

#define TclFreeIntRep(objPtr) \
	if ((objPtr)->typePtr != NULL) { \
		if ((objPtr)->typePtr->freeIntRepProc != NULL) { \
			(objPtr)->typePtr->freeIntRepProc(objPtr); \
		} \
		(objPtr)->typePtr = NULL; \
    }


/* utility function for invoking an obj command */
static int invoke(Tcl_Interp *interp, Tcl_Obj *obj1, Tcl_Obj *obj2, Tcl_Obj *obj3) {
	Tcl_Obj* cmdparts[3];
	cmdparts[0]=obj1;
	cmdparts[1]=obj2;
	cmdparts[2]=obj3;

	if (obj1 == NULL) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("Can't invoke NULL", -1));
		return TCL_ERROR;
	}

	int objc=3;
	if (obj3 == NULL) {
		objc = 2;
	}

	if (obj2 == NULL) {
		objc = 1;
	}

	for (int n=0; n<objc; n++) Tcl_IncrRefCount(cmdparts[n]);

	int code = Tcl_EvalObjv(interp, objc, cmdparts, TCL_EVAL_GLOBAL);

	for (int n=0; n<objc; n++) Tcl_DecrRefCount(cmdparts[n]);

	return code;
}


char * ckstrdup(const char *input) {
	int length = strlen(input);
	char *dup = ckalloc(length+1);
	strcpy(dup, input);
	return dup;
}

typedef struct {
	chid id; /* the EPICS connection ID for this channel */
	
	const char *name; /* PV name */
	
	/* Tcl command prefix which is invoked if the connection status changes */
	Tcl_Obj *connectprefix; 
} pvInfo;

static pvInfo *newpvInfo (chid id, const char *name, Tcl_Obj *prefix) {
	pvInfo *result=ckalloc(sizeof(pvInfo));
	result->id=id;
	result->name=ckstrdup(name);
	result->connectprefix = prefix;
	Tcl_IncrRefCount(prefix);
	return result;
}

static void freepvInfo(pvInfo *i) {
	ckfree(i->name);
	Tcl_DecrRefCount(i->connectprefix);
	ckfree(i);
}


/* Object command for a PV object */
static int InstanceCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
	pvInfo *info = (pvInfo *) clientData;
	/* Define subcommands put -callback & get -callback */
	/* For now, just return the connected PV name */
	Tcl_SetObjResult(interp, Tcl_NewStringObj(info->name, -1));
	return TCL_OK;
}

/* Create a new process variable object and return it
 * The callback is invoked for every change of the connection status */
static int ConnectCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
	static int pvcounter = 0; /* thread safe ?? */

	if (objc != 4) {
		Tcl_WrongNumArgs(interp, 1, objv, "<PV> -command <cmdprefix>");
		return TCL_ERROR;
	}
	
	Tcl_Obj *pvNameObj = objv[1];
	Tcl_Obj *cmdopt = objv[2];
	Tcl_Obj *cmdprefix = objv[3];
	
	/* check that 2nd arg == -command */
	if (strcmp(Tcl_GetString(cmdopt), "-command") != 0) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("Unknown option, must be -command", -1));
		return TCL_ERROR;
	}

	/* Create object name */
	char objName[50 + TCL_INTEGER_SPACE];
	sprintf(objName, "::AsynCA::pv%d", ++pvcounter);
	
	const char * pvName = Tcl_GetString(pvNameObj);

	pvInfo *info = newpvInfo(0, pvName, cmdprefix);

	Tcl_CreateObjCommand(interp, objName, InstanceCmd, (ClientData) info, NULL);
	Tcl_SetObjResult(interp, Tcl_NewStringObj(objName, -1));
	return TCL_OK;
}

int Asynca_Init(Tcl_Interp* interp) {
	if (interp == 0) return TCL_ERROR;

	if (Tcl_InitStubs(interp, TCL_VERSION, 0) == NULL) {
		return TCL_ERROR;
	}

	Tcl_PkgProvide(interp, PACKAGE_NAME, PACKAGE_VERSION);

	if (Tcl_Eval(interp, "namespace eval AsynCA {}")!=TCL_OK) {
		return TCL_ERROR;
	}
	
	Tcl_CreateObjCommand(interp, "AsynCA::connect", ConnectCmd, NULL, NULL);

	/* initialize EPICS library */
	if (ca_context_create (ca_enable_preemptive_callback) != ECA_NORMAL) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("Failure to initalize EPICS library", -1));
		return TCL_ERROR;
	}	
	
	return TCL_OK;
}

