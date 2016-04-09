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

TCLCLASSIMPLEMENT(AsynServer, test, test2);

// server instance
AsynServer::AsynServer (ClientData cdata, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	if (objc != 1) {
		Tcl_WrongNumArgs(interp, 1, objv, "");
		throw(TCL_ERROR);
	}
}

AsynServer::~AsynServer() { printf("Asynserver dying!\n"); }

int AsynServer::test(int objc, Tcl_Obj * const objv[]) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("test", -1));
	return TCL_OK;
}

int AsynServer::test2(int objc, Tcl_Obj * const objv[]) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("test2", -1));
	return TCL_OK;
}


