/*
 * caCmd.c --
 */

#include "caCmd.h"

#include <string.h>
#include <stdlib.h>
#ifdef HAVE_STDINT_H
# include <stdint.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif


char * ckstrdup(const char *input) {
	int length = strlen(input);
	char *dup = ckalloc(length+1);
	strcpy(dup, input);
	return dup;
}

static inline void DecrIfNotNull(Tcl_Obj** o) {
	if (*o) {
		Tcl_DecrRefCount(*o);
		*o=NULL;
	}
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
	result->monitorid = 0;
	result->monitorprefix = NULL;

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
	/* Remove all pending events in the Tcl event queue
	 * pointing to this PV */
	Tcl_DeleteEvents(PVeventDeleteProc, (ClientData)i);

	if (i->id) {
		if (i->monitorid) {
			ca_clear_subscription(i->monitorid);
		}
		ca_clear_channel(i->id);
	}
	ckfree(i->name);
	
	DecrIfNotNull(&i->connectprefix);
	DecrIfNotNull(&i->monitorprefix);
	ckfree(i);
}

static int PVeventDeleteProc(Tcl_Event *e, ClientData cdata) {
	if (e->proc == stateHandlerInvoke || e->proc == getHandlerInvoke || e->proc == putHandlerInvoke) {
		/* one of the EPICS events */
		PVevent *pev = (PVevent *) e;
		pvInfo *info_to_remove = (pvInfo *) cdata;
		if (pev->info == info_to_remove) {
			return 1;
		}
	}
	return 0;
}

static void stateHandler (struct connection_handler_args chargs) {
	/* callback */
	pvInfo *info = ca_puser(chargs.chid);
	/* queue event to handle the Tcl callback */
	connectionEvent * cev = ckalloc(sizeof(connectionEvent));
	
	cev->ev.proc = stateHandlerInvoke;
	cev->info = info;
	cev->op = chargs.op;
	Tcl_ThreadQueueEvent(info->thrid, (Tcl_Event*)cev, TCL_QUEUE_TAIL);
	Tcl_ThreadAlert(info->thrid);
}

static int stateHandlerInvoke(Tcl_Event* p, int flags) {
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
	"chid",
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
	TYPE,
	CHID
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
		case CHID:
			Tcl_SetObjResult(interp, Tcl_NewWideIntObj((intptr_t)info->id));
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
	if (objc != 5 && objc != 3) {
		Tcl_WrongNumArgs(interp, 2, objv, "<value> ?-command <cmdprefix>?");
		return TCL_ERROR;
	}

	int callback = (objc == 5);

	Tcl_Obj *value  = objv[2];
	Tcl_Obj *cmdopt = NULL;
	Tcl_Obj *cmdprefix = NULL;
	
	if (callback) {
		cmdopt = objv[3];
		cmdprefix = objv[4];

		/* check that the option is -command */
		if (strcmp(Tcl_GetString(cmdopt), "-command") != 0) {
			Tcl_SetObjResult(interp, Tcl_NewStringObj("Unknown option, must be -command", -1));
			return TCL_ERROR;
		}
	}

	/* Convert value into the EPICS record format */
	void *dbr;
	chtype puttype;
	if (GetEpicsValueFromObj(interp, value, info->type, info->nElem, &puttype, &dbr) != TCL_OK) {
		return TCL_ERROR;
	}

	//printf("Data type %d, native %d\n", (int)puttype, (int)(info->type));
	
	/* Issue put operation */
	putEvent *ev = ckalloc(sizeof(putEvent));
	ev->info = info;
	ev->putCmdPrefix = cmdprefix;
	if (cmdprefix) Tcl_IncrRefCount(ev->putCmdPrefix);
	ev->code = TCL_OK;

	int code = ca_array_put_callback (puttype, info->nElem, info->id, dbr, putHandler, ev);
	
	CACHECKTCL(ckfree(dbr); Tcl_DecrRefCount(ev->putCmdPrefix); ckfree(ev));

	code = ca_flush_io();

	CACHECKTCL(ckfree(dbr));

	ckfree(dbr);
	return TCL_OK;
}

static void putHandler(struct event_handler_args args) {
	/* put operation is finished */
	putEvent *pev = args.usr;

	if (pev->putCmdPrefix) {
		pvInfo *info = pev->info;

		pev->ev.proc=putHandlerInvoke;
		pev->code = args.status;
		Tcl_ThreadQueueEvent(info->thrid, (Tcl_Event*)pev, TCL_QUEUE_TAIL);
		Tcl_ThreadAlert(info->thrid);
	} else {
		ckfree(pev);
	}
}

static int  putHandlerInvoke(Tcl_Event *p, int flags) {
	/* the event handler run from Tcl */
	putEvent *ev = (putEvent *)p;
	pvInfo *info = ev->info;

	/* if the script is empty, ignore */
	if (ev->putCmdPrefix == NULL) { return 1; }

	Tcl_Obj *script = Tcl_DuplicateObj(ev->putCmdPrefix);
	Tcl_IncrRefCount(script);

	/* append result data and metadata dict */
	int code = Tcl_ListObjAppendElement(info->interp, script, Tcl_NewWideIntObj(ev->code));
	
	if (code != TCL_OK) {
		goto bgerr;
	}

	code = Tcl_ListObjAppendElement(info->interp, script, Tcl_NewStringObj(ca_message(ev->code), -1));
	
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
	Tcl_DecrRefCount(script);

	Tcl_AddErrorInfo(info->interp, "\n    (epics put callback script)");
	Tcl_BackgroundException(info->interp, code);

	/* this event was successfully handled */
	return 1;

}


static int GetEpicsValueFromObj(Tcl_Interp *interp, Tcl_Obj *obj, chtype type, long count, chtype *otype, void **dbr) {
	/* scalar values */
	if (count < 1) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("assert failed (count >= 1)", -1));
		return TCL_ERROR;
	}

	if (count == 1) {
		/* scalar values */
		switch (type) {
#define INTVALCONV(CHTYPE, CTYPE) \
			case CHTYPE: { \
							 Tcl_WideInt val;\
							 if (Tcl_GetWideIntFromObj(interp, obj, &val) != TCL_OK) {\
								 return TCL_ERROR;\
							 }\
							 CTYPE tval = val;\
							 \
							 if (tval != val) {\
								 /* value doesn't fit */\
								 Tcl_SetObjResult(interp, Tcl_NewStringObj("Value outside range for type " STRINGIFY(CHTYPE), -1));\
								 return TCL_ERROR;\
							 }\
							 CTYPE *dbuf = ckalloc(sizeof(CTYPE));\
							 *dbuf = tval; \
							 *dbr = dbuf;\
							 *otype = type; \
							 return TCL_OK; \
						 }

			INTVALCONV(DBR_CHAR, dbr_char_t)
			INTVALCONV(DBR_SHORT, dbr_short_t)
			INTVALCONV(DBR_LONG, dbr_long_t)
#undef INTVALCONV
			case DBR_DOUBLE: {
				 double val;
				 if (Tcl_GetDoubleFromObj(interp, obj, &val) != TCL_OK) {
					 return TCL_ERROR;
				 }

				 dbr_double_t *vptr = ckalloc(sizeof(dbr_double_t));
				 *vptr = val;

				 *dbr = vptr;
				 *otype = type;
				 return TCL_OK;
			}
			case DBR_FLOAT:  {				
				 double val;
				 if (Tcl_GetDoubleFromObj(interp, obj, &val) != TCL_OK) {
					 return TCL_ERROR;
				 }

				 dbr_float_t *vptr = ckalloc(sizeof(dbr_float_t));
				 *vptr = val;

				 *dbr = vptr;
				 *otype = type;
				 return TCL_OK;

			 }
			case DBR_ENUM: {
				long val;
				if (Tcl_GetLongFromObj(NULL, obj, &val) != TCL_OK) {
					/* if it's not an int, put the value as a string 
					 * Note: May fail, if an enum string is set to a decimal integer */
					dbr_string_t *vptr = ckalloc(sizeof(dbr_string_t));
					int len;
					char *buf = Tcl_GetStringFromObj(obj, &len);
					if (len+1 > MAX_ENUM_STRING_SIZE) {
						Tcl_SetObjResult(interp, Tcl_NewStringObj("String too long for enum", -1));
						return TCL_ERROR;
					}
					strncpy(*vptr, buf, len+1);
					//printf("Putting string %s\n", *vptr);
					*dbr = vptr;
					*otype = DBR_STRING;
					return TCL_OK;
				}

				dbr_enum_t *vptr = ckalloc(sizeof(dbr_enum_t));
				*vptr = val;

				*dbr = vptr;
				*otype = type;
				return TCL_OK;
		   }
			default: {
				Tcl_SetObjResult(interp, Tcl_NewStringObj("Unsupported data type", -1));
				return TCL_ERROR;
			}
		}
	} else {
		/* vector values */
#ifndef HAVE_VECTCL
		Tcl_SetObjResult(interp, Tcl_NewStringObj("Vector put not implemented", -1));
		return TCL_ERROR;
#endif
	}
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

	chtype gettype = GetTypeFromNative(info->type);
	/* retrieve enums as string */
	int code = ca_array_get_callback (gettype, info->nElem, info->id, getHandler, ev);
	//printf("Data type %d\n", (int)(gettype));
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

static chtype GetTypeFromNative(chtype type) {
	if (type == DBR_ENUM) {
		/* request enums as string */
		return DBR_TIME_STRING;
	}
	return dbf_type_to_DBR_TIME(type);
}


static void getHandler(struct event_handler_args args) {
	/* the callback exec'ed from EPICS 
	 * get succeeded */
	getEvent *ev = args.usr;
	pvInfo *info = ev->info;

	if ( args.status == ECA_NORMAL ) {
		ev->code = TCL_OK;
	} else {
		ev->code = TCL_ERROR;
		/* TODO background exception 
		 * Must be postponed into Tcl event handler */
	}
	
	/* Convert result into Tcl_Obj */
    ev->data = EpicsValue2Tcl(args);
	Tcl_IncrRefCount(ev->data);
	
	ev->metadata = EpicsMeta2Tcl(args);
	Tcl_IncrRefCount(ev->metadata);
	
	ev->ev.proc=getHandlerInvoke;
	
	Tcl_ThreadQueueEvent(info->thrid, (Tcl_Event*)ev, TCL_QUEUE_TAIL);
	Tcl_ThreadAlert(info->thrid);
}

static int  getHandlerInvoke(Tcl_Event *p, int flags) {
	/* the event handler run from Tcl */
	getEvent *ev = (getEvent *)p;
	pvInfo *info = ev->info;

	/* if the script is empty, ignore */
	if (ev->getCmdPrefix == NULL) { return 1; }

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

static Tcl_Obj * EpicsValue2Tcl(struct event_handler_args args)
{
	
	/* Return an empty object if the return value is invalid */
	if (!args.dbr || args.status != ECA_NORMAL) {
		return Tcl_NewObj();
	}
#ifndef HAVE_VECTCL
	/* Construct an ordinary Tcl list */
	switch (args.type) {
	case DBR_TIME_DOUBLE:{
			struct dbr_time_double *vptr = ((struct dbr_time_double *)args.dbr);
			if (args.count == 1) {
				return Tcl_NewDoubleObj(vptr->value);
			} else {
				double         *dptr = &vptr->value;
				unsigned	n;
				Tcl_Obj        *result = Tcl_NewObj();
				for (n = 0; n < args.count; n++) {
					Tcl_ListObjAppendElement(NULL, result, Tcl_NewDoubleObj(dptr[n]));
				}
				return result;
			}
		}
	case DBR_TIME_FLOAT:{
			struct dbr_time_float *vptr = ((struct dbr_time_float *)args.dbr);
			if (args.count == 1) {
				return Tcl_NewDoubleObj(vptr->value);
			} else {
				float          *dptr = &vptr->value;
				unsigned	n;
				Tcl_Obj        *result = Tcl_NewObj();
				for (n = 0; n < args.count; n++) {
					Tcl_ListObjAppendElement(NULL, result, Tcl_NewDoubleObj(dptr[n]));
				}
				return result;
			}
		}
#define INTVALCONV(CHTYPE, STYPE, CTYPE) \
		case CHTYPE:  {\
						  struct STYPE *vptr = ((struct STYPE *)args.dbr);\
						  if (args.count ==1) { \
							  return Tcl_NewLongObj(vptr->value); \
						  } else { \
							  unsigned n;\
							  CTYPE *dptr = &vptr->value; \
							  Tcl_Obj *result=Tcl_NewObj(); \
							  for (n=0; n<args.count; n++) {\
								  Tcl_ListObjAppendElement(NULL, result, Tcl_NewLongObj(dptr[n]));\
							  }\
							  return result;\
						  } \
					  }
		INTVALCONV(DBR_TIME_ENUM, dbr_time_enum, dbr_enum_t)
		INTVALCONV(DBR_TIME_CHAR, dbr_time_char, dbr_char_t)
		INTVALCONV(DBR_TIME_SHORT, dbr_time_short, dbr_short_t)
		INTVALCONV(DBR_TIME_LONG, dbr_time_long, dbr_long_t)
#undef INTVALCONV
	case DBR_TIME_STRING:{
			return Tcl_NewStringObj(((struct dbr_time_string *)args.dbr)->value, -1);
		}
	}

#endif


	return Tcl_NewStringObj("Some vector value", -1);
}

static Tcl_Obj * EpicsMeta2Tcl(struct event_handler_args args) {
	Tcl_Obj *meta = Tcl_NewDictObj();
	switch (args.type) {
#define TIMETYPE(CHTYPE, CTYPE) \
		case CHTYPE: {\
			if (args.dbr && args.status == ECA_NORMAL) { \
				struct CTYPE val = *((struct CTYPE*)args.dbr);\
				Tcl_DictObjPut(NULL, meta, Tcl_NewStringObj("status", -1), Tcl_NewLongObj(val.status));\
				Tcl_DictObjPut(NULL, meta, Tcl_NewStringObj("severity", -1), Tcl_NewLongObj(val.severity));\
				/* convert time stamp into float seconds from Unix epoch */\
				double ftime = val.stamp.secPastEpoch;\
				ftime += POSIX_TIME_AT_EPICS_EPOCH;\
				ftime += val.stamp.nsec*1e-9;\
				Tcl_DictObjPut(NULL, meta, Tcl_NewStringObj("time", -1), Tcl_NewDoubleObj(ftime));\
			} else { \
				/* no valid data obtained. Put empty objects into the dict */\
				Tcl_DictObjPut(NULL, meta, Tcl_NewStringObj("status", -1), Tcl_NewObj());\
				Tcl_DictObjPut(NULL, meta, Tcl_NewStringObj("severity", -1), Tcl_NewObj());\
				Tcl_DictObjPut(NULL, meta, Tcl_NewStringObj("time", -1), Tcl_NewObj());\
			} \
			/* put request status converted into string */\
\
			Tcl_DictObjPut(NULL, meta, Tcl_NewStringObj("reqstatus", -1), Tcl_NewLongObj(args.status));\
			Tcl_DictObjPut(NULL, meta, Tcl_NewStringObj("message", -1), Tcl_NewStringObj(ca_message(args.status), -1));\
			break;\
		}
		TIMETYPE(DBR_TIME_STRING, dbr_time_string);
		TIMETYPE(DBR_TIME_SHORT, dbr_time_short);
		TIMETYPE(DBR_TIME_FLOAT, dbr_time_float);
		TIMETYPE(DBR_TIME_ENUM, dbr_time_enum);
		TIMETYPE(DBR_TIME_CHAR, dbr_time_char);
		TIMETYPE(DBR_TIME_LONG, dbr_time_long);
		TIMETYPE(DBR_TIME_DOUBLE, dbr_time_double);

	}
	
	
	return meta;
}

/* Mutex to protect the monitor script; may not change while
 * the callback runs */

TCL_DECLARE_MUTEX(monitorMutex);

static int MonitorCmd(Tcl_Interp *interp, pvInfo *info, int objc, Tcl_Obj * const objv[]) {
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
	
	Tcl_MutexLock(&monitorMutex);
	/* Delete any previous monitor */
	int code;
	if (info->monitorid != 0) {
		code = ca_clear_subscription(info->monitorid);
		info->monitorid = 0;
		DecrIfNotNull(&info->monitorprefix);
		CACHECKTCL(Tcl_MutexUnlock(&monitorMutex));
	}
	
	/* If the command is the empty string, we are done */
	if (strcmp(Tcl_GetString(cmdprefix), "") == 0) {
		Tcl_MutexUnlock(&monitorMutex);
		return TCL_OK;
	}
	
	/* Else setup the monitor with a */
	info->monitorprefix = cmdprefix;
	Tcl_IncrRefCount(cmdprefix);
	chtype gettype = GetTypeFromNative(info->type);
	code = ca_create_subscription(gettype, info->nElem, info->id, DBE_VALUE, 
		monitorHandler, info, &info->monitorid);
	
	CACHECKTCL(DecrIfNotNull(&info->monitorprefix); Tcl_MutexUnlock(&monitorMutex));
	
	code = ca_flush_io();
	CACHECKTCL(Tcl_MutexUnlock(&monitorMutex));

	Tcl_MutexUnlock(&monitorMutex);
	return TCL_OK;

}

static void monitorHandler(struct event_handler_args args) {
	/* the callback exec'ed from EPICS 
	 * monitor indicates a changed value*/
	Tcl_MutexLock(&monitorMutex);
	
	pvInfo *info = args.usr;

	if (info->monitorprefix == NULL) {
		/* we are done */
		Tcl_MutexUnlock(&monitorMutex);
		return;
	}

	/* schedule a getEvent */
	getEvent *ev = ckalloc(sizeof(getEvent));
	ev->info = info;
	ev->getCmdPrefix = info->monitorprefix;
	Tcl_IncrRefCount(ev->getCmdPrefix);

	if ( args.status == ECA_NORMAL ) {
		ev->code = TCL_OK;
	} else {
		ev->code = TCL_ERROR;
		/* TODO background exception 
		 * Must be postponed into Tcl event handler */
	}

	/* Convert result into Tcl_Obj */
    ev->data = EpicsValue2Tcl(args);
	Tcl_IncrRefCount(ev->data);
	
	ev->metadata = EpicsMeta2Tcl(args);
	Tcl_IncrRefCount(ev->metadata);
	
	ev->ev.proc=getHandlerInvoke;

	Tcl_ThreadQueueEvent(info->thrid, (Tcl_Event*)ev, TCL_QUEUE_TAIL);
	Tcl_ThreadAlert(info->thrid);

	Tcl_MutexUnlock(&monitorMutex);
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
	Tcl_CreateObjCommand(interp, "::AsynCA::server", startServerCmd, NULL, NULL);

	/* initialize EPICS library */
	if (ca_context_create (ca_enable_preemptive_callback) != ECA_NORMAL) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("Failure to initalize EPICS library", -1));
		return TCL_ERROR;
	}	
	
	return TCL_OK;
}

