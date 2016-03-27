#ifndef CACMD_H
#define CACMD_H

#include <cadef.h>
#undef INLINE /* conflicting definition from Tcl and EPICS */
#include <tcl.h>
#include <string.h>
#include <stdlib.h>
/* Process variable object */
typedef struct {
	chid id; /* the EPICS connection ID for this channel */
	
	const char *name; /* PV name */
	
	/* Tcl command prefix which is invoked if the connection status changes */
	Tcl_Obj *connectprefix;
	Tcl_Interp *interp;
	Tcl_ThreadId thrid;
	int connected;
	unsigned nElem;
	chtype type;
} pvInfo;


static void freepvInfo(pvInfo *i);
static int newpvInfo (Tcl_Interp *interp, const char *name, Tcl_Obj *prefix, pvInfo **info);
static void DeleteCmd(ClientData cdata);


typedef struct {
	Tcl_Event ev;
	pvInfo *info;
	long op;
} connectionEvent;

void stateHandler (struct connection_handler_args chargs);
int stateHandlerInvoke(Tcl_Event* p, int flags);


static int PutCmd(Tcl_Interp *interp, pvInfo *info, int objc, Tcl_Obj * const objv[]);

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
static Tcl_Obj * EpicsTime2Tcl(struct event_handler_args args);

static int MonitorCmd(Tcl_Interp *interp, pvInfo *info, int objc, Tcl_Obj * const objv[]);

#endif
