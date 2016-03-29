lappend auto_path [file dirname [file dirname [info script]]]
package require AsynCA

proc pvack {pv status} {
	if {$status} { set ::connected $pv }
}

proc cainfo {pv} {
	puts [$pv name]
	puts "[$pv nElem] elements"
	puts "[$pv type]"
}

set pv [AsynCA::connect jane -command pvack]
vwait connected

cainfo $pv

proc tie {cmd args} {
	$cmd $args
}

proc newvalue {pv} {
	$pv monitor -command [list tie [info coroutine]]
	for {set i 0} {$i<20} {incr i} {
		lassign [yield] value meta
		puts $value
	}
	$pv monitor -command {}
}

coroutine newjane newvalue $pv

#proc print {args} {
#	puts $args
#}

