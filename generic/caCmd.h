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

void stateHandler (struct connection_handler_args chargs);

typedef struct {
	Tcl_Event ev;
	pvInfo *info;
	long op;
} connectionEvent;

int stateHandlerInvoke(Tcl_Event* p, int flags);


static int PutCmd(Tcl_Interp *interp, pvInfo *info, int objc, Tcl_Obj * const objv[]);

typedef struct {
	Tcl_Event ev;
	pvInfo *info;
	Tcl_Obj *data;
	Tcl_Obj *metadata;
} getEvent;

static int GetCmd(Tcl_Interp *interp, pvInfo *info, int objc, Tcl_Obj * const objv[]);
static int MonitorCmd(Tcl_Interp *interp, pvInfo *info, int objc, Tcl_Obj * const objv[]);

#endif
