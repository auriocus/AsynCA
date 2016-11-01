# AsynCA  - a Tcl extension for EPICS channel access

AsynCA is a library which provides bindings to the [EPICS channel access protocol](http://aps.anl.gov/epics/)
It exploits Tcl's event loop to integrate events from EPICS in a fully
asynchronous way. AsynCA can both act as a client and as a server. Writing channel access servers
has never been easier ;)

## Basic usage as a client
The lines below show how to connect to a PV served from the example server excas:

{% highlight tcl %}
{% raw %}
	package require AsynCA
    
	# example callback for demonstration purposes
	proc echo {args} { puts $args }

    set pv [AsynCA::connect jane -command echo]
    # upon connect, the callback is executed
	# afterwards, write and read to the PV
	# asynchronous write (caput)
	$pv put 3.14
	# asynchronous write with a callback on completion (caput_callback)
	$pv put 3.14 -command echo
	# asynchronous read, value received in the callback
	$pv get -command echo
	
	# arrange a callback to fire each time that jane has a new value
	$pv monitor -command echo
{% endraw %}
{% endhighlight %}

Due to the event loop integration, the callbacks do NOT run in a separate thread, i.e. there cannot 
be any race conditions, and all variables are accessible. However, it also means that the callbacks
should return after a short time. Otherwise, multiple events in the queue can pile up, exactly the same
way as Tk events or fileevents. 

## Synchronous functions 
AsynCA also provides a few higher level synchronous commands scripted in Tcl based on the asynchronous functions,
which can substantially simplify the code. The following code demonstrates how to connect to multiple PVs
and write / read to them using these functions

{% highlight tcl %}
{% raw %}
	package require AsynCA
	
	# connect to jane and bill
	set PVs [AsynCA::connectwait jane bill]
	# the PVs are returned as a dict
	set bill [dict get $PVs bill]
	set jane [dict get $PVs jane]

	# read from jane, and write the result to bill
	set val [AsynCA::read $jane]
	AsynCA::putwait $bill $val
	
	# read from bill & jane. waiting for both values
	lassign [AsynCA::readmultiple $jane $bill] jval bval

	# write the same values back, wait for both
	AsynCA::putwait $jane $jval $bill $bval
{% endraw %}
{% endhighlight %}


In this version, the waiting is implemented using "vwait". A better interface using Tcl's coroutines in
version 8.6 will be implemented in the next version. However, these coroutine-aware functions can then only
be used inside of a coroutine. Currently it is not yet decided how exactly this will be implemented. Possible
choices are the futures package or ycl coro relay. 


## Usage as a server
AsynCA also wraps the API to write EPICS server tools, i.e. you can create process variables from Tcl. 
PVs can either be served from an internal memory, to which the Tcl script writes to / reads from, or 
in an asynchronous fashion similar to Tcl's socket API.
Basic usage as a server is demonstrated below

{% highlight tcl %}
{% raw %}
	package require AsynCA

	# create server. This opens a UDP socket and starts broadcasting
	# magic packets. There should only be one server running per process
	set s [AsynCA::server]

	# create a scalar double-valued PV
	set pv [$s createPV jane]
	
	# set it to an initial value
	$pv write 3.5

	# create another PV which can be written to asynchronously
	set pvasync [$s createPV jasync]
	$pvasync writecommand writetopv

	# handle the writing
	proc writetopv {req} {
		# the proc gets a request object as a parameter
		# query the value that the client wishes to write
		puts "Write request: [$req value]"

		# now to accept the write, change the PV
		$::pvasync write [$req value]
		
		# signal the client, that the request is fully processed
		$req return
	}


	# create a PV which can be read asynchronously
	set pvasyncread [$s createPV jasyncread]
	$pvasyncread readcommand readfrompv

	proc readfrompv {req} {
		# the request object must be returned a value to finish 
		# the request. Here, we return the current time
		set val [clock microseconds]

		# 3 s in the future
		after 3000 [list $req return $val]
	}
{% endraw %}
{% endhighlight %}

