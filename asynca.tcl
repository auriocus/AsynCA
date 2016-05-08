namespace eval ::AsynCA {
	
	variable ns [namespace current]
	variable managed_PVs {}

	proc connectwait {args} {
		variable ns
		variable managed_PVs

		set result {}
		foreach p $args {
			if {[dict exists $managed_PVs $p]} {
				dict set result $p [dict get $managed_PVs $p]
			} else {
				set PV [connect $p -command ${ns}::connectcall]
				dict set managed_PVs $p {}
				dict set result $p $PV
			}
		}

		while {![allconnected]} {
			vwait ${ns}::managed_PVs
		}

		return $result 
	}

	proc connectcall {PV status} {
		variable managed_PVs
		dict set managed_PVs [$PV name] $PV
	}


	proc allconnected {} {
		variable managed_PVs
		expr {[dict size [dict filter $managed_PVs value {}]] == 0}
	}

	variable pendingwrites {}
	proc putwait {PVs values} {
		variable ns
		variable pendingwrites

		foreach p $PVs v $values {
			$p put $v -command [list ${ns}::writefinish $p]
			dict set pendingwrites $p 1
		}

		while {![allwritesfinished]} {
			vwait ${ns}::pendingwrites
		}
	
	}

	proc allwritesfinished {} {
		variable pendingwrites
		expr {[dict size [dict filter $pendingwrites value 1]] == 0}
	}

	proc writefinish {pv args} {
		variable pendingwrites
		dict set pendingwrites $pv 0
	}

	variable pendingreads {}
	proc readmultiple {args} {
		variable ns
		variable pendingreads
		foreach p $args {
			$p get -command [list ${ns}::readfinish $p]
			dict set pendingreads $p {}
		}

		while {![allreadsfinished]} {
			vwait ${ns}::pendingreads
		}
	
		dict values $pendingreads
	}

	proc allreadsfinished {} {
		variable pendingreads
		expr {[dict size [dict filter $pendingreads value {}]] == 0}
	}

	proc readfinish {pv value status} {
		variable pendingreads
		dict set pendingreads $pv $value
	}
		
	proc read {PV} {
		# read a single PV synchronously
		lindex [readmultiple $PV] 0
	}
}
