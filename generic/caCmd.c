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
	result->connected = 0;
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
/*	printf("Callback from connection handler\n");
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
	}  */

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
	
	if (cev->op == CA_OP_CONN_UP) {
		info->connected = 1;
		/* Retrieve information about type and number of elements */
		info->nElem = ca_element_count(info->id);
		info->type  = ca_field_type(info->id);
	} else {
		info->connected = 0;
	}
	
	code = Tcl_ListObjAppendElement(info->interp, script, Tcl_NewBooleanObj(info->connected));
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

const char * pvcmdtable[] = {
	"destroy",
	"put",
	"get",
	"monitor",
	"name",
	"connected",
	"nElem",
	"type",
	NULL
};

enum PVCMD {
	DESTROY = 0,
	PUT,
	GET,
	MONITOR,
	NAME,
	CONNECTED,
	NELEM,
	TYPE
};

/* Object command for a PV object */
static int InstanceCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	pvInfo *info = (pvInfo *) clientData;

	if (objc<2) {
		Tcl_WrongNumArgs(interp, 1, objv, "subcommand");
		return TCL_ERROR;
	}
	Tcl_Obj *subcommand=objv[1];
	int cmdindex;
	if (Tcl_GetIndexFromObj(interp, subcommand, pvcmdtable, "subcommand", 0, &cmdindex) != TCL_OK) {
		return TCL_ERROR;
	}
	switch (cmdindex) {
		case PUT:
			return PutCmd(interp, info, objc, objv);
		case GET:
			return GetCmd(interp, info, objc, objv);
		case MONITOR:
			return MonitorCmd(interp, info, objc, objv);
		case NAME:
			Tcl_SetObjResult(interp, Tcl_NewStringObj(info->name, -1));
			return TCL_OK;
		case CONNECTED:
			Tcl_SetObjResult(interp, Tcl_NewBooleanObj(info->connected));
			return TCL_OK;
		case NELEM:
			Tcl_SetObjResult(interp, Tcl_NewWideIntObj(info->nElem));
			return TCL_OK;
		case TYPE:
			Tcl_SetObjResult(interp, Tcl_NewStringObj(dbr_type_to_text(info->type), -1));
			return TCL_OK;
		case DESTROY: {
			Tcl_Command self = Tcl_GetCommandFromObj(interp, objv[0]);
			if (self != NULL) {
				Tcl_DeleteCommandFromToken(interp, self);
			}
			return TCL_OK;
		}
		default:
			Tcl_SetObjResult(interp, Tcl_NewStringObj("Unknown error", -1));
			return TCL_ERROR;
	}
			
}

static int PutCmd(Tcl_Interp *interp, pvInfo *info, int objc, Tcl_Obj * const objv[]) {
	return TCL_OK;
}

static int GetCmd(Tcl_Interp *interp, pvInfo *info, int objc, Tcl_Obj * const objv[]) {
	if (objc != 4) {
		Tcl_WrongNumArgs(interp, 2, objv, "-command <cmdprefix>");
		return TCL_ERROR;
	}
	
	Tcl_Obj *cmdopt = objv[2];
	Tcl_Obj *cmdprefix =objv[3];

	/* check that the option is -command */
	if (strcmp(Tcl_GetString(cmdopt), "-command") != 0) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("Unknown option, must be -command", -1));
		return TCL_ERROR;
	}


	/* request a read with the native datatype */
	getEvent *ev = ckalloc(sizeof(getEvent));
	ev->info = info;
	ev->getCmdPrefix = cmdprefix;
	Tcl_IncrRefCount(cmdprefix);

	int code = ca_array_get_callback (info->type, info->nElem, info->id, getHandler, ev);
	if (code != ECA_NORMAL) {
		/* raise error */
		ckfree(ev);
		Tcl_DecrRefCount(cmdprefix);
		Tcl_SetObjResult(interp, Tcl_NewStringObj(ca_message(code), -1));
		return TCL_ERROR;
	}	
	
	code = ca_flush_io();
	if (code != ECA_NORMAL) {
		/* raise error */
		Tcl_SetObjResult(interp, Tcl_NewStringObj(ca_message(code), -1));
		return TCL_ERROR;
	}

	return TCL_OK;
}

static void getHandler(struct event_handler_args args) {
	/* the callback exec'ed from EPICS 
	 * get succeeded */
	getEvent *ev = args.usr;
	pvInfo *info = ev->info;
	if ( args.status != ECA_NORMAL ) {
		/* TODO background exception 
		 * Must be postponed into Tcl event handler */
		ev->code = TCL_ERROR;
	}
	/* Convert result into Tcl_Obj */
    ev->data = EpicsValue2Tcl(args);
	Tcl_IncrRefCount(ev->data);
	Tcl_Obj *timestamp = EpicsTime2Tcl(args);
	ev->metadata = Tcl_NewDictObj();
	Tcl_IncrRefCount(ev->metadata);
	Tcl_DictObjPut(info->interp, ev->metadata, Tcl_NewStringObj("time", -1), timestamp);
	
	ev->ev.proc=getHandlerInvoke;
	ev->code = TCL_OK;
	Tcl_ThreadQueueEvent(info->thrid, (Tcl_Event*)ev, TCL_QUEUE_TAIL);
	Tcl_ThreadAlert(info->thrid);
}

static int  getHandlerInvoke(Tcl_Event *p, int flags) {
	/* the event handler run from Tcl */
	getEvent *ev = (getEvent *)p;
	pvInfo *info = ev->info;
	Tcl_Obj *script = Tcl_DuplicateObj(ev->getCmdPrefix);
	Tcl_IncrRefCount(script);
	/* append result data and metadata dict */
	int code = Tcl_ListObjAppendElement(info->interp, script, ev->data);
	if (code != TCL_OK) {
		goto bgerr;
	}
	
	code = Tcl_ListObjAppendElement(info->interp, script, ev->metadata);
	if (code != TCL_OK) {
		goto bgerr;
	}

	Tcl_Preserve(info->interp);
	code = Tcl_EvalObjEx(info->interp, script, TCL_EVAL_GLOBAL);

	if (code != TCL_OK) { goto bgerr; }

	Tcl_Release(info->interp);
	Tcl_DecrRefCount(ev->data);
	Tcl_DecrRefCount(ev->metadata);
	Tcl_DecrRefCount(script);
	/* this event was successfully handled */
	return 1; 
bgerr:
	/* put error in background */
	Tcl_DecrRefCount(ev->data);
	Tcl_DecrRefCount(ev->metadata);
	Tcl_DecrRefCount(script);
	
	Tcl_AddErrorInfo(info->interp, "\n    (epics get callback script)");
	Tcl_BackgroundException(info->interp, code);
	
	/* this event was successfully handled */
	return 1;
}

static Tcl_Obj * EpicsValue2Tcl(struct event_handler_args args) {
	if (args.count == 1) {
		/* scalar conversion */
		switch (args.type) {
			case DBR_DOUBLE: {
				dbr_double_t val = *((dbr_double_t *)args.dbr);
				return Tcl_NewDoubleObj(val);
			}
			case DBR_FLOAT:  {
				dbr_float_t val = *((dbr_float_t *)args.dbr);
				return Tcl_NewDoubleObj(val);
			}
			default:
				return Tcl_NewStringObj("Some scalar value", -1);
		}
	}

	return Tcl_NewStringObj("Some vector value", -1);
}

static Tcl_Obj * EpicsTime2Tcl(struct event_handler_args args) {
	return Tcl_NewDoubleObj(3.1415926);
}

static int MonitorCmd(Tcl_Interp *interp, pvInfo *info, int objc, Tcl_Obj * const objv[]) {
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
	Tcl_CreateObjCommand(interp, objName, InstanceCmd, (ClientData) info, DeleteCmd);
	
	Tcl_SetObjResult(interp, Tcl_NewStringObj(objName, -1));
	return TCL_OK;
}

static void DeleteCmd(ClientData cdata) {
	freepvInfo((pvInfo *)cdata);
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

