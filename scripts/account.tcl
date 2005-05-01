# shroudBNC - an object-oriented framework for IRC
# Copyright (C) 2005 Gunnar Beutner
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

# configuration variables: ::account354

foreach user [split $::account354] {
	setctx $user
	bind join - * auth:join
	bind raw - 354 auth:raw354
}

internalbind pulse auth:pulse

proc auth:join {nick host hand chan} {
	if {[isbotnick $nick]} {
		putserv "WHO $chan n%nat,23"
	} else {
		if {[getchanlogin $nick] == ""} {
			putserv "WHO $nick n%nat,23"
		} else {
			sbnc:callbinds "account" - $chan "$chan $host" $nick $host $hand $chan
		}
	}
}

proc auth:raw354 {source raw text} {
	set pieces [split $text]

	if {[lindex $pieces 1] != 23} { return }

	set nick [lindex $pieces 2]
	set site [getchanhost $nick]
	set host [lindex $pieces 2]!$site
	set hand [finduser $host]

	foreach chan [internalchannels] {
		if {[onchan $nick $chan]} {
			set old [bncgettag $chan $nick account]
			bncsettag $chan $nick account [lindex $pieces 3]

			if {$old == "" || ($old == 0 && [lindex $pieces 3] != 0)} {
				sbnc:callbinds "account" - $chan "$chan $host" $nick $site $hand $chan
			}
		}
	}
}

proc auth:pulse {time} {
	global account354

	if {$time % 120 != 0} { return }

	foreach user [split $account354] {
		setctx $user

		set nicks ""
		foreach chan [internalchannels] {
			foreach nick [internalchanlist $chan] {
				set acc [bncgettag $chan $nick account]
				if {$acc == "" || ($acc == 0 && $time % 240 == 0)} {
					lappend nicks $nick
				}

				if {[string length [join [sbnc:uniq $nicks] ","]] > 300} {
					putserv "WHO [join [sbnc:uniq $nicks] ","] n%nat,23"
					set nicks ""
				}
			}
		}

		if {$nicks != ""} {
			putserv "WHO [join [sbnc:uniq $nicks] ","] n%nat,23"
		}
	}
}

proc getchanlogin {nick {channel ""}} {
	foreach chan [internalchannels] {
		set acc [bncgettag $chan $nick account]

		if {$acc != ""} {
			return $acc
		}
	}

	return ""
}
