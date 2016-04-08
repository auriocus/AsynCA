/*
 * caCmd.c --
 */

#include "casCmd.hpp"

/* Object command for a PV object */
static int InstanceCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	if (objc<2) {
		Tcl_WrongNumArgs(interp, 1, objv, "subcommand");
		return TCL_ERROR;
	}
	return TCL_OK;
}

static void DeleteCmd(ClientData cdata) {
	/* delete underlying object */
}

/* Create a new process variable object and return it
 * The callback is invoked for every change of the connection status */
int createPVCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
	static int pvcounter = 0; /* thread safe ?? */

	if (objc != 2) {
		Tcl_WrongNumArgs(interp, 1, objv, "<PV>");
		return TCL_ERROR;
	}
	
	Tcl_Obj *pvNameObj = objv[1];
	const char * pvName = Tcl_GetString(pvNameObj);
	/* create PV */
	casPV *pv = NULL;
	/* Create object name */
	char objName[50 + TCL_INTEGER_SPACE];
	sprintf(objName, "::AsynCA::pvs%d", ++pvcounter);
	Tcl_CreateObjCommand(interp, objName, InstanceCmd, (ClientData) pv, DeleteCmd);
	
	Tcl_SetObjResult(interp, Tcl_NewStringObj(objName, -1));
	return TCL_OK;
}

int startServerCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	/* Start thread with the EPICS event loop */
	Tcl_SetObjResult(interp, Tcl_ObjPrintf("This should start the extra thread"));
	return TCL_OK;
}

