lappend auto_path [file dirname [file dirname [info script]]]
package require AsynCA

proc pvack {pv status} {
	if {$status} { set ::connected $pv }
}

proc cainfo {pv} {
	puts [$pv name]
	puts "[$pv nelem] elements"
	puts "[$pv type]"
}

set pv [AsynCA::connect jane -command pvack]
vwait connected

cainfo $pv

