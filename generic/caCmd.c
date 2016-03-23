/*
 * caCmd.c --
 */

#include "caCmd.h"

char * ckstrdup(const char *input) {
	int length = strlen(input);
	char *dup = ckalloc(length+1);
	strcpy(dup, input);
	return dup;
}


static int newpvInfo (Tcl_Interp *interp, const char *name, Tcl_Obj *prefix, pvInfo **info) {
	pvInfo *result=ckalloc(sizeof(pvInfo));
	*info = result;
	
	result->interp=interp;
	result->name=ckstrdup(name);
	Tcl_IncrRefCount(prefix);
	result->connectprefix = prefix;
	result->id = 0;
	result->thrid = Tcl_GetCurrentThread();

	/* connect PV */
	int code = ca_create_channel(name, stateHandler, result, 0, &(result->id));
	if (code != ECA_NORMAL) {
		/* raise error */
		freepvInfo(result);
		Tcl_SetObjResult(interp, Tcl_NewStringObj(ca_message(code), -1));
		return TCL_ERROR;
	}	

	return TCL_OK;
}

static void freepvInfo(pvInfo *i) {
	if (i->id) ca_clear_channel(i->id);
	ckfree(i->name);
	Tcl_DecrRefCount(i->connectprefix);
	ckfree(i);
}


void stateHandler (struct connection_handler_args chargs) {
	/* callback */
	pvInfo *info = ca_puser(chargs.chid);
	printf("Callback from connection handler\n");
	printf("PV %s ", info->name);
	switch (chargs.op) {
		case CA_OP_CONN_UP: {
			printf(" up\n");
			break;
		}
		case CA_OP_CONN_DOWN: {
			printf(" down\n");
			break;
		}
		default: {
			printf("Unkown opcode %ld\n", chargs.op);
		}
	}

	/* queue event to handle the Tcl callback */
	connectionEvent * cev = ckalloc(sizeof(connectionEvent));
	
	cev->ev.proc = stateHandlerInvoke;
	cev->info = info;
	cev->op = chargs.op;
	Tcl_ThreadQueueEvent(info->thrid, (Tcl_Event*)cev, TCL_QUEUE_TAIL);
	Tcl_ThreadAlert(info->thrid);
}

int stateHandlerInvoke(Tcl_Event* p, int flags) {
	/* called from Tcl event loop, when the connection status changes */
	connectionEvent *cev =(connectionEvent *) p;
	pvInfo *info = cev->info;
	Tcl_Obj *script = Tcl_DuplicateObj(info->connectprefix);
	Tcl_IncrRefCount(script);
	/* append name of PV and up/down */
	int code = Tcl_ListObjAppendElement(info->interp, script, Tcl_NewStringObj(info->name, -1));
	if (code != TCL_OK) {
		goto bgerr;
	}
	
	int connected;
	if (cev->op == CA_OP_CONN_UP) {
		connected = 1;
	} else {
		connected = 0;
	}
	
	code = Tcl_ListObjAppendElement(info->interp, script, Tcl_NewBooleanObj(connected));
	if (code != TCL_OK) {
		goto bgerr;
	}


	Tcl_Preserve(info->interp);
	code = Tcl_EvalObjEx(info->interp, script, TCL_EVAL_GLOBAL);

	if (code != TCL_OK) { goto bgerr; }

	Tcl_Release(info->interp);
	Tcl_DecrRefCount(script);
	/* this event was successfully handled */
	return 1; 
bgerr:
	/* put error in background */
	Tcl_AddErrorInfo(info->interp, "\n    (epics connection callback script)");
	Tcl_BackgroundException(info->interp, code);
	
	/* this event was successfully handled */
	return 1;
}

/* Object command for a PV object */
static int InstanceCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
	pvInfo *info = (pvInfo *) clientData;
	/* Define subcommands:
	 * put value -command <cmd>
	 * get -command <cmd>
	 * monitor <cmd>
	 */
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

	const char * pvName = Tcl_GetString(pvNameObj);
	/* connect PV */
	
	pvInfo *info;
	int code = newpvInfo(interp, pvName, cmdprefix, &info);
	if (code != TCL_OK) {
		return TCL_ERROR;
	}

	/* Create object name */
	char objName[50 + TCL_INTEGER_SPACE];
	sprintf(objName, "::AsynCA::pv%d", ++pvcounter);
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

	if (Tcl_Eval(interp, "namespace eval ::AsynCA {}")!=TCL_OK) {
		return TCL_ERROR;
	}
	
	Tcl_CreateObjCommand(interp, "::AsynCA::connect", ConnectCmd, NULL, NULL);

	/* initialize EPICS library */
	if (ca_context_create (ca_enable_preemptive_callback) != ECA_NORMAL) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("Failure to initalize EPICS library", -1));
		return TCL_ERROR;
	}	
	
	return TCL_OK;
}

