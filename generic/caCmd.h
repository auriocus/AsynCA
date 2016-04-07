#ifndef CACMD_H
#define CACMD_H

#include <cadef.h>
#undef INLINE /* conflicting definition from Tcl and EPICS */
#include <tcl.h>
#include <string.h>
#include <stdlib.h>
#ifdef HAVE_STDINT_H
# include <stdint.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

/* caCheckTcl raises an error if code is an EPICS error */
#define CACHECKTCL(cleanup) \
	if (code != ECA_NORMAL) { \
		cleanup; \
		Tcl_DecrRefCount(cmdprefix); \
		Tcl_SetObjResult(interp, Tcl_NewStringObj(ca_message(code), -1)); \
		return TCL_ERROR; \
	} 


/* Process variable object */
typedef struct {
	Tcl_Mutex mutex;
	chid id; /* the EPICS connection ID for this channel */
	
	const char *name; /* PV name */
	
	/* Tcl command prefix which is invoked if the connection status changes */
	Tcl_Obj *connectprefix;
	Tcl_Obj *monitorprefix;
	evid monitorid;
	Tcl_Interp *interp;
	Tcl_ThreadId thrid;
	int connected;
	unsigned nElem;
	chtype type;
} pvInfo;


static void freepvInfo(pvInfo *i);
static int newpvInfo (Tcl_Interp *interp, const char *name, Tcl_Obj *prefix, pvInfo **info);
static void DeleteCmd(ClientData cdata);
static int PVeventDeleteProc(Tcl_Event *e, ClientData cdata);

typedef struct {
	Tcl_Event ev;
	pvInfo *info;
} PVevent;

typedef struct {
	Tcl_Event ev;
	pvInfo *info;
	long op;
} connectionEvent;

static void stateHandler (struct connection_handler_args chargs);
static int stateHandlerInvoke(Tcl_Event* p, int flags);

typedef struct {
	Tcl_Event ev;
	pvInfo *info;
	Tcl_Obj *putCmdPrefix;
	int code;
} putEvent;

static int PutCmd(Tcl_Interp *interp, pvInfo *info, int objc, Tcl_Obj * const objv[]);

static void putHandler(struct event_handler_args args); /* the callback exec'ed from EPICS */
static int  putHandlerInvoke(Tcl_Event *p, int flags); /* the event handler run from Tcl */

static int GetEpicsValueFromObj(Tcl_Interp *interp, Tcl_Obj *obj, chtype type, long count, chtype *otype, void **dbr);

static chtype GetTypeFromNative(chtype type);

static int GetCmd(Tcl_Interp *interp, pvInfo *info, int objc, Tcl_Obj * const objv[]);
typedef struct {
	Tcl_Event ev;
	pvInfo *info;
	Tcl_Obj *data;
	Tcl_Obj *metadata;
	Tcl_Obj *getCmdPrefix;
	int code; /* error code */
} getEvent;

static void getHandler(struct event_handler_args args); /* the callback exec'ed from EPICS */
static int  getHandlerInvoke(Tcl_Event *p, int flags); /* the event handler run from Tcl */

/* convert EPICS data into Tcl_Obj */
static Tcl_Obj * EpicsValue2Tcl(struct event_handler_args args);
static Tcl_Obj * EpicsMeta2Tcl(struct event_handler_args args);

/* convert Tcl_Obj into C representation for EPICS */
static int GetEpicsValueFromObj(Tcl_Interp *interp, Tcl_Obj *obj, chtype type, long count, chtype *otype, void **dbr);

static int MonitorCmd(Tcl_Interp *interp, pvInfo *info, int objc, Tcl_Obj * const objv[]);
static void monitorHandler(struct event_handler_args args); /* the callback exec'ed from EPICS */

#endif
