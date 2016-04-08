/*
 * caCmd.c --
 */

#include "casCmd.hpp"
#include <fdManager.h>


/* Create a new process variable object and return it
 * The callback is invoked for every change of the connection status */

int startServerCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {

	return AsynServerCreateCmd(clientData, interp, objc, objv);
#if 0
	/* Start thread with the EPICS event loop */
	Tcl_ThreadId id;    /* holds identity of thread created */
	Tcl_ThreadId mainid = Tcl_GetCurrentThread();
	
	AsynServer *aserver = new AsynServer(interp, objc, objv);

	if (Tcl_CreateThread(&id, EpicsEventLoop, static_cast<ClientData>(NULL),
				TCL_THREAD_STACK_DEFAULT,
				TCL_THREAD_NOFLAGS) != TCL_OK) {
		/* Thread did not create correctly */
		return TCL_ERROR;
	}
	
	/* All cleaned up nicely */
	Tcl_SetObjResult(interp, Tcl_ObjPrintf("Thread was started, id = %ld",reinterpret_cast<intptr_t>(id)));
	return TCL_OK;
#endif
}

static Tcl_ThreadCreateType EpicsEventLoop (ClientData clientData)
{
	//Tcl_ThreadId mainid = static_cast<Tcl_ThreadId> (clientData);
	/* Run epics select loop */
	while (true) {
		fileDescriptorManager.process(1000.0);
	}
	TCL_THREAD_CREATE_RETURN;
}

// server instance
AsynServer::AsynServer (Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	if (objc != 1) {
		Tcl_WrongNumArgs(interp, 1, objv, "");
		throw(TCL_ERROR);
	}
}

AsynServer::~AsynServer() { }

int AsynServer::test(Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("test", -1));
	return TCL_OK;
}

int AsynServer::test2(Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("test2", -1));
	return TCL_OK;
}

static int AsynServerCreateCmd(ClientData cdata, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	static int counter = 0;

	/* create Server */
	try {
		AsynServer *instance = new AsynServer(interp, objc, objv);

		/* Create object name */
		char objName[50 + TCL_INTEGER_SPACE];
		sprintf(objName, "::AsynCA::AsynServer%d", ++counter);
		Tcl_CreateObjCommand(interp, objName, AsynServerInstanceCmd, (ClientData) instance, AsynServerDeleteCmd);
		
		Tcl_SetObjResult(interp, Tcl_NewStringObj(objName, -1));

		return TCL_OK;

	} catch(int code) {
		return code;
	}
}


static int AsynServerInstanceCmd(ClientData cdata, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	if (objc < 2) {
		Tcl_WrongNumArgs(interp, 1, objv, "method");
		return TCL_ERROR;
	}

	int index;
	
	if (Tcl_GetIndexFromObjStruct(interp, objv[1], 
		AsynServerSubCmdTable, sizeof(AsynServerSubCmdEntry), 
		"method", 0, &index) != TCL_OK) {
		
		return TCL_ERROR;
	}	

	AsynServer *instance = static_cast<AsynServer *>(cdata);
	return (instance->*(AsynServerSubCmdTable[index].fun)) (interp, objc, objv);
}

static void AsynServerDeleteCmd(ClientData cdata) {
	delete static_cast<AsynServer *>(cdata);
}
