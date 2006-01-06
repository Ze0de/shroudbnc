/*******************************************************************************
 * shroudBNC - an object-oriented framework for IRC                            *
 * Copyright (C) 2005 Gunnar Beutner                                           *
 *                                                                             *
 * This program is free software; you can redistribute it and/or               *
 * modify it under the terms of the GNU General Public License                 *
 * as published by the Free Software Foundation; either version 2              *
 * of the License, or (at your option) any later version.                      *
 *                                                                             *
 * This program is distributed in the hope that it will be useful,             *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the               *
 * GNU General Public License for more details.                                *
 *                                                                             *
 * You should have received a copy of the GNU General Public License           *
 * along with this program; if not, write to the Free Software                 *
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA. *
 *******************************************************************************/

#include "StdAfx.h"
#include "sbnc.h"

extern loaderparams_s *g_LoaderParameters;

IMPL_DNSEVENTCLASS(CClientDnsEvents, CClientConnection, AsyncDnsFinishedClient);

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CClientConnection::CClientConnection(SOCKET Client, sockaddr_in Peer, bool SSL) : CConnection(Client, SSL, Role_Client) {
	m_Nick = NULL;
	m_Password = NULL;
	m_Username = NULL;

	m_Peer = Peer;
	m_PeerName = NULL;

	if (Client != INVALID_SOCKET) {
		InternalWriteLine(":Notice!notice@shroudbnc.org NOTICE * :*** shroudBNC" BNCVERSION);
		InternalWriteLine(":Notice!notice@shroudbnc.org NOTICE * :*** Looking up your hostname");

		m_DnsEvents = new CClientDnsEvents(this);

		adns_submit_reverse(g_adns_State, (const sockaddr*)&m_Peer, adns_r_ptr, (adns_queryflags)0, m_DnsEvents, &m_PeerA);

		m_AdnsTimeout = g_Bouncer->CreateTimer(3, true, AdnsTimeoutTimer, this);
	} else {
		m_AdnsTimeout = NULL;
		m_DnsEvents = NULL;
	}

	m_CommandList = NULL;
}

CClientConnection::CClientConnection(SOCKET Client, CAssocArray *Box, CBouncerUser *Owning) : CConnection(Client, false, Role_Client) {
	m_Owner = Owning;

	m_Nick = strdup(Box->ReadString("client.nick"));
	m_Password = NULL;
	m_Username = strdup(Owning->GetUsername());

	socklen_t SocketLen = sizeof(m_Peer);
	getpeername(Client, (sockaddr *)&m_Peer, &SocketLen);

	m_PeerName = strdup(Box->ReadString("client.peername"));

	m_Socket = (SOCKET)Box->ReadInteger("client.fd");

	m_AdnsTimeout = NULL;
	m_DnsEvents = NULL;

	m_CommandList = NULL;

	InitSocket();
}

CClientConnection::~CClientConnection() {
	free(m_Nick);
	free(m_Password);
	free(m_Username);
	free(m_PeerName);

	if (!m_PeerName && m_Socket != INVALID_SOCKET)
		adns_cancel(m_PeerA);

	if (m_AdnsTimeout)
		m_AdnsTimeout->Destroy();

	if (m_DnsEvents)
		m_DnsEvents->Destroy();
}

connection_role_e CClientConnection::GetRole(void) {
	return Role_Client;
}

bool CClientConnection::ProcessBncCommand(const char* Subcommand, int argc, const char** argv, bool NoticeUser) {
	char* Out;
	CBouncerUser* targUser = m_Owner;

#define SENDUSER(Text) { \
	if (NoticeUser) { \
		targUser->RealNotice(Text); \
	} else { \
		targUser->Notice(Text); \
	} \
	}

	if (argc < 1) {
		SENDUSER("Try /sbnc help");
		return false;
	}

	if (strcmpi(Subcommand, "help") == 0) {
		if (argc <= 1) {
			SENDUSER("--The following commands are available to you--");
			SENDUSER("--Used as '/sbnc <command>', or '/msg -sbnc <command>'");
		}

		FlushCommands(&m_CommandList);

		if (m_Owner->IsAdmin()) {
			AddCommand(&m_CommandList, "adduser", "Admin", "creates a new user",
				"Syntax: adduser <username> <password>\nCreates a new user.");
			AddCommand(&m_CommandList, "deluser", "Admin", "removes a user",
				"Syntax: deluser <username>\nDeletes a user.");
			AddCommand(&m_CommandList, "resetpass", "Admin", "sets a user's password",
				"Syntax: resetpass <user> <password>\nResets another user's password.");
			AddCommand(&m_CommandList, "who", "Admin", "shows users",
				"Syntax: who\nShows a list of all users.\nFlags (which are displayed in front of the username):\n"
				"@ user is an admin\n* user is currently logged in\n! user is suspended");
			AddCommand(&m_CommandList, "admin", "Admin", "gives someone admin privileges",
				"Syntax: admin <username>\nGives admin privileges to a user.");
			AddCommand(&m_CommandList, "unadmin", "Admin", "removes someone's admin privileges",
				"Syntax: unadmin <username>\nRemoves someone's admin privileges.");
			AddCommand(&m_CommandList, "suspend", "Admin", "suspends a user",
				"Syntax: suspend <username> [reason]\nSuspends an account. An optional reason can be specified.");
			AddCommand(&m_CommandList, "unsuspend", "Admin", "unsuspends a user",
				"Syntax: unsuspend <username>\nRemoves a suspension from the specified account.");
			AddCommand(&m_CommandList, "lsmod", "Admin", "lists loaded modules",
				"Syntax: lsmod\nLists all currently loaded modules.");
			AddCommand(&m_CommandList, "insmod", "Admin", "loads a module",
				"Syntax: insmod <filename>\nLoads a module.");
			AddCommand(&m_CommandList, "rmmod", "Admin", "unloads a module",
				"Syntax: rmmod <index>\nUnloads a module. Use the \"lsmod\" command to list all modules.");
			AddCommand(&m_CommandList, "simul", "Admin", "simulates a command on another user's connection",
				"Syntax: simul <username> <command>\nExecutes a command in another user's context.");
			AddCommand(&m_CommandList, "global", "Admin", "sends a global notice to all bouncer users",
				"Syntax: global <text>\nSends a notice to all currently connected users.");
			AddCommand(&m_CommandList, "kill", "Admin", "disconnects a user from the bouncer",
				"Syntax: kill <username>\nDisconnects a user from the bouncer.");
			AddCommand(&m_CommandList, "disconnect", "Admin", "disconnects a user from the irc server",
				"Syntax: disconnect <username>\nDisconnects a user from the IRC server which he is currently connected to.");
			AddCommand(&m_CommandList, "playmainlog", "Admin", "plays the bouncer's log",
				"Syntax: playmainlog\nDisplays the bouncer's log.");
			AddCommand(&m_CommandList, "erasemainlog", "Admin", "erases the bouncer's log",
				"Syntax: erasemainlog\nErases the bouncer's log.");
			AddCommand(&m_CommandList, "gvhost", "Admin", "sets the default/global vhost",
				"Syntax: gvhost <host>\nSets the bouncer's default vhost.");
			AddCommand(&m_CommandList, "motd", "Admin", "sets the bouncer's motd",
				"Syntax: motd [text]\nShows or modifies the motd.");
			AddCommand(&m_CommandList, "reload", "Admin", "reloads shroudBNC from a shared object file",
				"Syntax: reload <filename>\nReloads shroudBNC from a new .so file.");
			AddCommand(&m_CommandList, "die", "Admin", "terminates the bouncer",
				"Syntax: die\nTerminates the bouncer.");
		}

		AddCommand(&m_CommandList, "read", "User", "plays your message log",
			"Syntax: read\nDisplays your private log.");
		AddCommand(&m_CommandList, "erase", "User", "erases your message log",
			"Syntax: erase\nErases your private log.");
		AddCommand(&m_CommandList, "set", "User", "sets configurable options for your user",
			"Syntax: set [option] [value]\nDisplays or changes configurable options for your user.");
		AddCommand(&m_CommandList, "jump", "User", "reconnects to the irc server",
			"Syntax: jump\nReconnects to the irc server.");
		AddCommand(&m_CommandList, "hosts", "User", "lists all hostmasks, which are permitted to use this account",
			"Syntax: hosts\nLists all hosts which are permitted to use this account.");
		AddCommand(&m_CommandList, "hostadd", "User", "adds a hostmask",
			"Syntax: hostadd <host>\nAdds a host to your hostlist. E.g. *.tiscali.de");
		AddCommand(&m_CommandList, "hostdel", "User", "removes a hostmask",
			"Syntax: hostdel <host>\nRemoves a host from your hostlist.");
		AddCommand(&m_CommandList, "partall", "User", "parts all channels and tells sBNC not to rejoin them when you reconnect to a server",
			"Syntax: partall\nParts all channels and tells shroudBNC not to rejoin any channels when you reconnect to a"
			" server.\nThis might be useful if you get disconnected due to an \"Max sendq exceeded\" error.");
#ifdef USESSL
		AddCommand(&m_CommandList, "savecert", "User", "saves your current client certificate for use with public key authentication",
			"Syntax: savecert\nSaves your current client certificate for use with public key authentication.\n"
			"Once you have saved your certificate you can use it for logging in without a password.");
		AddCommand(&m_CommandList, "delcert", "User", "removes a certificate",
			"Syntax: delcert <id>\nRemoves the specified certificate.");
		AddCommand(&m_CommandList, "showcert", "User", "shows information about your certificates",
			"Syntax: showcert\nShows a list of certificates which can be used for logging in.");
#endif

		if (m_Owner->IsAdmin()) {
			AddCommand(&m_CommandList, "status", "User", "tells you the current status",
				"Syntax: status\nDisplays information about your user. This command is used for debugging.");
		}

		AddCommand(&m_CommandList, "help", "User", "displays a list of commands or information about individual commands",
			"Syntax: help [command]\nDisplays a list of commands or information about individual commands.");
	}

	CModule** Modules = g_Bouncer->GetModules();
	int Count = g_Bouncer->GetModuleCount();
	bool latchedRetVal = true;

	for (int i = 0; i < Count; i++) {
		if (!Modules[i]) { continue; }

		if (Modules[i]->InterceptClientCommand(this, Subcommand, argc, argv, NoticeUser))
			latchedRetVal = false;
	}

	if (strcmpi(Subcommand, "help") == 0) {
		if (argc <= 1) {
			// show help
			xhash_t<command_t *> *Hash;
			xhash_t<command_t *> *CommandList;
			int i = 0, Align = 0, Len;

			CommandList = (xhash_t<command_t *> *)malloc(sizeof(xhash_t<command_t *>) * m_CommandList->Count());

			while ((Hash = m_CommandList->Iterate(i++)) != NULL) {
				CommandList[i - 1] = *Hash;

				Len = strlen(Hash->Name);

				if (Len > Align)
					Align = Len;
			}

			qsort(CommandList, m_CommandList->Count(), sizeof(xhash_t<command_t *>), CmpCommandT);

			char *Category = NULL;
			char *Format;

			asprintf(&Format, "%%-%ds - %%s", Align);

			for (i = 0; i < m_CommandList->Count(); i++) {
				if (Category == NULL || strcmpi(CommandList[i].Value->Category, Category) != 0) {
					if (Category)
						SENDUSER("--");

					Category = CommandList[i].Value->Category;

					asprintf(&Out, "%s commands", Category);
					SENDUSER(Out);
					free(Out);
				}

				asprintf(&Out, Format, CommandList[i].Name, CommandList[i].Value->Description);
				SENDUSER(Out);
				free(Out);
			}

			FlushCommands(&m_CommandList);

			free(Format);
			free(CommandList);

			SENDUSER("End of HELP.");
		} else {
			command_t *Command = m_CommandList->Get(argv[1]);

			if (Command == NULL) {
				SENDUSER("There is no such command.");
			} else if (Command && Command->HelpText == NULL) {
				SENDUSER("No help is available for this command.");
			} else {
				char *Help = strdup(Command->HelpText);
				char *HelpBase = Help;

				while (true) {
					char *NextLine = strstr(Help, "\n");

					if (NextLine) {
						NextLine[0] = '\0';
						NextLine++;
					}

					SENDUSER(Help);

					if (NextLine == NULL)
						break;
					else
						Help = NextLine;
				}

				free(HelpBase);
			}
		}

		return false;
	}

	if (!latchedRetVal)
		return false;

	if (strcmpi(Subcommand, "lsmod") == 0 && m_Owner->IsAdmin()) {
		CModule** Modules = g_Bouncer->GetModules();
		int Count = g_Bouncer->GetModuleCount();

		for (int i = 0; i < Count; i++) {
			if (!Modules[i]) { continue; }

			asprintf(&Out, "%d: %x %s", i + 1, Modules[i]->GetHandle(), Modules[i]->GetFilename());

			if (Out == NULL) {
				LOGERROR("asprintf() failed.");

				continue;
			}

			SENDUSER(Out);
			free(Out);
		}

		SENDUSER("End of MODULES.");

		return false;
	} else if (strcmpi(Subcommand, "insmod") == 0 && m_Owner->IsAdmin()) {
		if (argc < 2) {
			SENDUSER("Syntax: INSMOD module-path");
			return false;
		}

		const char *Error = NULL;

		CModule* Module = g_Bouncer->LoadModule(argv[1], &Error);

		if (Module != NULL) {
			SENDUSER("Module was loaded.");
		} else {
			asprintf(&Out, "Module could not be loaded: %s", Error);

			if (Out == NULL) {
				LOGERROR("asprintf() failed");

				return false;
			}

			SENDUSER(Out);

			free(Out);
		}

		return false;
	} else if (strcmpi(Subcommand, "rmmod") == 0 && m_Owner->IsAdmin()) {
		if (argc < 2) {
			SENDUSER("Syntax: RMMOD module-id");
			return false;
		}

		int idx = atoi(argv[1]);

		if (idx < 1 || idx > g_Bouncer->GetModuleCount()) {
			SENDUSER("There is no such module.");
		} else {
			CModule* Mod = g_Bouncer->GetModules()[idx - 1];

			if (!Mod) {
				SENDUSER("This module is already unloaded.");
			} else {
				if (g_Bouncer->UnloadModule(Mod)) {
					SENDUSER("Done.");
				} else {
					SENDUSER("Failed to unload this module.");
				}
			}
		}

		return false;
	} else if (strcmpi(Subcommand, "set") == 0) {
		CBouncerConfig* Config = m_Owner->GetConfig();
		if (argc < 3) {
			SENDUSER("Configurable settings:");
			SENDUSER("--");

			SENDUSER("password - Set");

			asprintf(&Out, "vhost - %s", m_Owner->GetVHost() ? m_Owner->GetVHost() : "Default");
			if (Out == NULL) {
				LOGERROR("asprintf() failed.");
			} else {
				SENDUSER(Out);
				free(Out);
			}

			asprintf(&Out, "server - %s:%d", m_Owner->GetServer(), m_Owner->GetPort());
			if (Out == NULL) {
				LOGERROR("asprintf() failed.");
			} else {
				SENDUSER(Out);
				free(Out);
			}

			asprintf(&Out, "serverpass - %s", m_Owner->GetServerPassword() ? "Set" : "Not set");
			if (Out == NULL) {
				LOGERROR("asprintf() failed.");
			} else {
				SENDUSER(Out);
				free(Out);
			}

			asprintf(&Out, "realname - %s", m_Owner->GetRealname());
			if (Out == NULL) {
				LOGERROR("asprintf() failed.");
			} else {
				SENDUSER(Out);
				free(Out);
			}

			asprintf(&Out, "awaynick - %s", m_Owner->GetAwayNick() ? m_Owner->GetAwayNick() : "Not set");
			if (Out == NULL) {
				LOGERROR("asprintf() failed.");
			} else {
				SENDUSER(Out);
				free(Out);
			}

			asprintf(&Out, "away - %s", m_Owner->GetAwayText() ? m_Owner->GetAwayText() : "Not set");
			if (Out == NULL) {
				LOGERROR("asprintf() failed.");
			} else {
				SENDUSER(Out);
				free(Out);
			}

			asprintf(&Out, "appendtimestamp - %s", Config->ReadInteger("user.ts") ? "On" : "Off");
			if (Out == NULL) {
				LOGERROR("asprintf() failed.");
			} else {
				SENDUSER(Out);
				free(Out);
			}

			asprintf(&Out, "usequitasaway - %s", Config->ReadInteger("user.quitaway") ? "On" : "Off");
			if (Out == NULL) {
				LOGERROR("asprintf() failed.");
			} else {
				SENDUSER(Out);
				free(Out);
			}

#ifdef USESSL
			asprintf(&Out, "ssl - %s", m_Owner->GetSSL() ? "On" : "Off");
			if (Out == NULL) {
				LOGERROR("asprintf() failed.");
			} else {
				SENDUSER(Out);
				free(Out);
			}
#endif

			const char* AutoModes = m_Owner->GetAutoModes();
			bool ValidAutoModes = AutoModes && *AutoModes;
			const char* DropModes = m_Owner->GetDropModes();
			bool ValidDropModes = DropModes && *DropModes;

			const char *AutoModesPrefix = "+", *DropModesPrefix = "-";

			if (!ValidAutoModes || (AutoModes && *AutoModes == '+'))
				AutoModesPrefix = "";

			if (!ValidDropModes || (DropModes && *DropModes == '-'))
				DropModesPrefix = "";

			asprintf(&Out, "automodes - %s%s", AutoModesPrefix, ValidAutoModes ? AutoModes : "Not set");
			if (Out == NULL) {
				LOGERROR("asprintf() failed.");
			} else {
				SENDUSER(Out);
				free(Out);
			}

			asprintf(&Out, "dropmodes - %s%s", DropModesPrefix, ValidDropModes ? DropModes : "Not set");
			if (Out == NULL) {
				LOGERROR("asprintf() failed.");
			} else {
				SENDUSER(Out);
				free(Out);
			}
		} else {
			if (strcmpi(argv[1], "server") == 0) {
				if (argc > 3) {
					m_Owner->SetServer(argv[2]);
					m_Owner->SetPort(atoi(argv[3]));

					m_Owner->ScheduleReconnect(0);
				} else {
					SENDUSER("Syntax: /sbnc set server host port");

					return false;
				}
			} else if (strcmpi(argv[1], "realname") == 0) {
				ArgRejoinArray(argv, 2);
				m_Owner->SetRealname(argv[2]);
			} else if (strcmpi(argv[1], "awaynick") == 0) {
				m_Owner->SetAwayNick(argv[2]);
			} else if (strcmpi(argv[1], "away") == 0) {
				ArgRejoinArray(argv, 2);
				m_Owner->SetAwayText(argv[2]);
			} else if (strcmpi(argv[1], "vhost") == 0) {
				m_Owner->SetVHost(argv[2]);
			} else if (strcmpi(argv[1], "serverpass") == 0) {
				m_Owner->SetServerPassword(argv[2]);
			} else if (strcmpi(argv[1], "password") == 0) {
				if (strlen(argv[2]) < 6 || argc > 3) {
					SENDUSER("Your password is too short or contains invalid characters.");
					return false;
				} else {
					m_Owner->SetPassword(argv[2]);
				}
			} else if (strcmpi(argv[1], "appendtimestamp") == 0) {
				if (strcmpi(argv[2], "on") == 0)
					Config->WriteInteger("user.ts", 1);
				else if (strcmpi(argv[2], "off") == 0)
					Config->WriteInteger("user.ts", 0);
				else {
					SENDUSER("Value must be either 'on' or 'off'.");

					return false;
				}
			} else if (strcmpi(argv[1], "usequitasaway") == 0) {
				if (strcmpi(argv[2], "on") == 0)
					Config->WriteInteger("user.quitaway", 1);
				else if (strcmpi(argv[2], "off") == 0)
					Config->WriteInteger("user.quitaway", 0);
				else {
					SENDUSER("Value must be either 'on' or 'off'.");

					return false;
				}
			} else if (strcmpi(argv[1], "automodes") == 0) {
				ArgRejoinArray(argv, 2);
				m_Owner->SetAutoModes(argv[2]);
			} else if (strcmpi(argv[1], "dropmodes") == 0) {
				ArgRejoinArray(argv, 2);
				m_Owner->SetDropModes(argv[2]);
			} else if (strcmpi(argv[1], "ssl") == 0) {
				if (strcmpi(argv[2], "on") == 0)
					m_Owner->SetSSL(true);
				else if (strcmpi(argv[2], "off") == 0)
					m_Owner->SetSSL(false);
				else {
					SENDUSER("Value must be either 'on' or 'off'.");

					return false;
				}
			} else {
				SENDUSER("Unknown setting");
				return false;
			}

			SENDUSER("Done.");
		}

		return false;
#ifdef USESSL
	} else if (strcmpi(Subcommand, "savecert") == 0) {
		if (!IsSSL()) {
			SENDUSER("Error: You are not using an SSL-encrypted connection.");
		} else if (GetPeerCertificate() == NULL) {
			SENDUSER("Error: You are not using a client certificate.");
		} else {
			m_Owner->AddClientCertificate(GetPeerCertificate());

			SENDUSER("Your certificate was stored and will be used for public key authentication.");
		}

		return false;
	} else if (strcmpi(Subcommand, "showcert") == 0) {
		int i = 0;
		char Buffer[300];
		X509* ClientCert;
		X509_NAME* name;
		bool First = true;

		while ((ClientCert = (X509*)m_Owner->GetClientCertificate(i++)) != NULL) {
			if (ClientCert == NULL && First) {
				SENDUSER("You did not set a client certificate.");

				return false;
			}

			if (!First) {
				SENDUSER("---");
			}

			First = false;

			asprintf(&Out, "Client Certificate #%d", i);

			if (Out == NULL) {
				LOGERROR("asprintf() failed.");

				return false;
			}

			SENDUSER(Out);

			free(Out);

			name = X509_get_issuer_name(ClientCert);
			X509_NAME_oneline(name, Buffer, sizeof(Buffer));
			asprintf(&Out, "issuer: %s", Buffer);

			if (Out == NULL) {
				LOGERROR("asprintf() failed.");

				return false;
			}

			SENDUSER(Out);
			free(Out);

			name = X509_get_subject_name(ClientCert);
			X509_NAME_oneline(name, Buffer, sizeof(Buffer));
			asprintf(&Out, "subject: %s", Buffer);

			if (Out == NULL) {
				LOGERROR("asprintf() failed.");

				return false;
			}

			SENDUSER(Out);
			free(Out);
		}

		SENDUSER("End of CERTIFICATES.");

		return false;
	} else if (strcmpi(Subcommand, "delcert") == 0) {
		int id;

		if (argc < 2) {
			SENDUSER("Syntax: delcert ID");
			
			return false;
		}

		id = atoi(argv[1]);

		X509* Cert = (X509*)m_Owner->GetClientCertificate(id - 1);

		if (Cert != NULL) {
			if (m_Owner->RemoveClientCertificate(Cert)) {
				SENDUSER("Done.");
			} else {
				SENDUSER("An error occured while removing the certificate.");
			}
		} else {
			SENDUSER("The ID you specified is not valid. Use the SHOWCERT command to get a list of valid IDs.");
		}

		return false;
#endif
	} else if (strcmpi(Subcommand, "die") == 0 && m_Owner->IsAdmin()) {
		g_Bouncer->Log("Shutdown requested by %s", m_Owner->GetUsername());
		g_Bouncer->Shutdown();

		return false;
	} else if (strcmpi(Subcommand, "reload") == 0 && m_Owner->IsAdmin()) {
		if (argc < 2) {
			SENDUSER("Syntax: RELOAD sbnc-module.so");
			return false;
		}

		g_Bouncer->GetLoaderParameters()->SetModule(argv[1]);
		g_Bouncer->Log("Reload requested by %s", m_Owner->GetUsername());
		g_Bouncer->InitializeFreeze();

		return false;
	} else if (strcmpi(Subcommand, "adduser") == 0 && m_Owner->IsAdmin()) {
		if (argc < 3) {
			SENDUSER("Syntax: ADDUSER username password");
			return false;
		}

		if (!g_Bouncer->IsValidUsername(argv[1])) {
			SENDUSER("Could not create user: The username must be alpha-numeric.");
			return false;
		}

		g_Bouncer->CreateUser(argv[1], argv[2]);

		SENDUSER("Done.");

		return false;

	} else if (strcmpi(Subcommand, "deluser") == 0 && m_Owner->IsAdmin()) {
		if (argc < 2) {
			SENDUSER("Syntax: DELUSER username");
			return false;
		}

		g_Bouncer->RemoveUser(argv[1]);

		SENDUSER("Done.");

		return false;
	} else if (strcmpi(Subcommand, "simul") == 0 && m_Owner->IsAdmin()) {
		if (argc < 3) {
			SENDUSER("Syntax: SIMUL username :command");
			return false;
		}

		CBouncerUser* User = g_Bouncer->GetUser(argv[1]);

		if (User) {
			ArgRejoinArray(argv, 2);
			User->Simulate(argv[2], this);

			SENDUSER("Done.");
		} else {
			asprintf(&Out, "No such user: %s", argv[1]);
			if (Out == NULL) {
				LOGERROR("asprintf() failed.");
			} else {
				SENDUSER(Out);
				free(Out);
			}
		}

		return false;
	} else if (strcmpi(Subcommand, "direct") == 0) {
		if (argc < 2) {
			SENDUSER("Syntax: DIRECT :command");
			return false;
		}

		CIRCConnection* IRC = m_Owner->GetIRCConnection();

		ArgRejoinArray(argv, 1);
		IRC->InternalWriteLine(argv[1]);

		return false;
	} else if (strcmpi(Subcommand, "gvhost") == 0 && m_Owner->IsAdmin()) {
		if (argc < 2) {
			const char* Ip = g_Bouncer->GetConfig()->ReadString("system.vhost");

			asprintf(&Out, "Current global VHost: %s", Ip ? Ip : "(none)");
			if (Out == NULL) {
				LOGERROR("asprintf() failed.");
			} else {
				SENDUSER(Out);
				free(Out);
			}
		} else {
			g_Bouncer->GetConfig()->WriteString("system.vhost", argv[1]);
			SENDUSER("Done.");
		}

		return false;
	} else if (strcmpi(Subcommand, "motd") == 0) {
		if (argc < 2) {
			const char* Motd = g_Bouncer->GetMotd();

			asprintf(&Out, "Current MOTD: %s", Motd ? Motd : "(none)");
			if (Out == NULL) {
				LOGERROR("asprintf() failed.");
			} else {
				SENDUSER(Out);
				free(Out);
			}
		} else if (m_Owner->IsAdmin()) {
			ArgRejoinArray(argv, 1);
			g_Bouncer->SetMotd(argv[1]);
			SENDUSER("Done.");
		}

		return false;
	} else if (strcmpi(Subcommand, "global") == 0 && m_Owner->IsAdmin()) {
		if (argc < 2) {
			SENDUSER("Syntax: GLOBAL :text");
			return false;
		}

		ArgRejoinArray(argv, 1);
		g_Bouncer->GlobalNotice(argv[1]);
		return false;
	} else if (strcmpi(Subcommand, "kill") == 0 && m_Owner->IsAdmin()) {
		if (argc < 2) {
			SENDUSER("Syntax: KILL username");
			return false;
		}

		CBouncerUser *User = g_Bouncer->GetUser(argv[1]);

		if (User && User->GetClientConnection()) {
			User->GetClientConnection()->Kill("Requested.");
			SENDUSER("Done.");
		} else {
			SENDUSER("There is no such user or that user is not currently logged in.");
		}

		return false;
	} else if (strcmpi(Subcommand, "disconnect") == 0 && m_Owner->IsAdmin()) {
		if (argc < 2) {
			SENDUSER("Syntax: disconnect username");
			return false;
		}

		asprintf(&Out, "SBNC SIMUL %s :PERROR :Requested.", argv[1]);
		if (Out == NULL) {
			LOGERROR("asprintf() failed.");
		} else {
			ParseLine(Out);
			free(Out);
		}

		SENDUSER("Done.");

		return false;
	} else if (strcmpi(Subcommand, "jump") == 0) {
		if (m_Owner->GetIRCConnection()) {
			m_Owner->GetIRCConnection()->Kill("Reconnecting");

			m_Owner->SetIRCConnection(NULL);
		}

		m_Owner->ScheduleReconnect(2);
		return false;
	} else if (strcmpi(Subcommand, "status") == 0) {
		asprintf(&Out, "Username: %s", m_Owner->GetUsername());
		if (Out == NULL) {
			LOGERROR("asprintf() failed.");
		} else {
			SENDUSER(Out);
			free(Out);
		}

		SENDUSER("This is shroudBNC " BNCVERSION);

		asprintf(&Out, "You are %san admin.", m_Owner->IsAdmin() ? "" : "not ");
		if (Out == NULL) {
			LOGERROR("asprintf() failed.");
		} else {
			SENDUSER(Out);
			free(Out);
		}

		asprintf(&Out, "Client: sendq: %d, recvq: %d", SendqSize(), RecvqSize());
		if (Out == NULL) {
			LOGERROR("asprintf() failed.");
		} else {
			SENDUSER(Out);
			free(Out);
		}

		CIRCConnection* IRC = m_Owner->GetIRCConnection();

		if (IRC) {
			asprintf(&Out, "IRC: sendq: %d, recvq: %d", IRC->SendqSize(), IRC->RecvqSize());
			if (Out == NULL) {
				LOGERROR("asprintf() failed.");
			} else {
				SENDUSER(Out);
				free(Out);
			}

			SENDUSER("Channels:");

			int a = 0;

			while (xhash_t<CChannel*>* Chan = IRC->GetChannels()->Iterate(a++)) {
				SENDUSER(Chan->Name);
			}

			SENDUSER("End of CHANNELS.");
		}

		asprintf(&Out, "Uptime: %d seconds", m_Owner->IRCUptime());
		if (Out == NULL) {
			LOGERROR("asprintf() failed.");
		} else {
			SENDUSER(Out);
			free(Out);
		}

		return false;
	} else if (strcmpi(Subcommand, "impulse") == 0 && m_Owner->IsAdmin()) {
		if (argc < 2) {
			SENDUSER("Syntax: impulse command");

			return false;
		}

		const char *Reply = g_Bouncer->DebugImpulse(atoi(argv[1]));

		if (Reply != NULL) {
			SENDUSER(Reply);
		} else {
			SENDUSER("No return value.");
		}

		return false;
	} else if (strcmpi(Subcommand, "who") == 0 && m_Owner->IsAdmin()) {
		int i = 0;

		while (xhash_t<CBouncerUser *> *UserHash = g_Bouncer->GetUsers()->Iterate(i++)) {
			const char* Server, *ClientAddr;
			CBouncerUser* User = UserHash->Value;

			if (!User)
				continue;

			if (User->GetIRCConnection())
				Server = User->GetIRCConnection()->GetServer();
			else
				Server = NULL;

			if (User->GetClientConnection())
				ClientAddr = User->GetClientConnection()->GetPeerName();
			else
				ClientAddr = NULL;

			char LastSeen[1024];

			if (User->GetLastSeen() == 0)
				strcpy(LastSeen, "Never");
			else if (User->GetClientConnection() != NULL)
				strcpy(LastSeen, "Now");
			else {
				tm Then;
				time_t tThen = User->GetLastSeen();

				Then = *localtime(&tThen);

#ifdef _WIN32
				strftime(LastSeen, sizeof(LastSeen), "%#c" , &Then);
#else
				strftime(LastSeen, sizeof(LastSeen), "%c" , &Then);
#endif
			}

			asprintf(&Out, "%s%s%s%s(%s)@%s [%s] [Last seen: %s] :%s", User->IsLocked() ? "!" : "", User->IsAdmin() ? "@" : "", ClientAddr ? "*" : "", User->GetUsername(), User->GetIRCConnection() ? (User->GetIRCConnection()->GetCurrentNick() ? User->GetIRCConnection()->GetCurrentNick() : "<none>") : User->GetNick(), ClientAddr ? ClientAddr : "", Server ? Server : "", LastSeen, User->GetRealname());
			if (Out == NULL) {
				LOGERROR("asprintf() failed.");
			} else {
				SENDUSER(Out);
				free(Out);
			}
		}

		SENDUSER("End of USERS.");

		return false;
	} else if (strcmpi(Subcommand, "read") == 0) {
		m_Owner->GetLog()->PlayToUser(m_Owner, NoticeUser);

		if (NoticeUser)
			m_Owner->RealNotice("End of LOG. Use '/sbnc erase' to remove this log.");
		else
			m_Owner->Notice("End of LOG. Use '/msg -sBNC erase' to remove this log.");

		return false;
	} else if (strcmpi(Subcommand, "erase") == 0) {
		m_Owner->GetLog()->Clear();
		SENDUSER("Done.");

		return false;
	} else if (strcmpi(Subcommand, "playmainlog") == 0 && m_Owner->IsAdmin()) {
		g_Bouncer->GetLog()->PlayToUser(m_Owner, NoticeUser);

		if (NoticeUser)
			m_Owner->RealNotice("End of LOG. Use /sbnc erasemainlog to remove this log.");
		else
			m_Owner->Notice("End of LOG. Use /msg -sBNC erasemainlog to remove this log.");

		return false;
	} else if (strcmpi(Subcommand, "erasemainlog") == 0 && m_Owner->IsAdmin()) {
		g_Bouncer->GetLog()->Clear();
		g_Bouncer->Log("User %s erased the main log", m_Owner->GetUsername());
		SENDUSER("Done.");

		return false;
	} else if (strcmpi(Subcommand, "hosts") == 0) {
		char** Hosts = m_Owner->GetHostAllows();
		unsigned int a = 0;

		SENDUSER("Hostmasks");
		SENDUSER("---------");

		for (unsigned int i = 0; i < m_Owner->GetHostAllowCount(); i++) {
			if (Hosts[i]) {
				SENDUSER(Hosts[i]);
				a++;
			}
		}

		if (a == 0)
			SENDUSER("*");

		SENDUSER("End of HOSTS.");

		return false;
	} else if (strcmpi(Subcommand, "hostadd") == 0) {
		if (argc <= 1) {
			SENDUSER("Syntax: HOSTADD hostmask");

			return false;
		}

		char** Hosts = m_Owner->GetHostAllows();
		unsigned int a = 0;

		for (unsigned int i = 0; i < m_Owner->GetHostAllowCount(); i++) {
			if (Hosts[i])
				a++;
		}

		if (m_Owner->CanHostConnect(argv[1]) && a) {
			SENDUSER("This hostmask is already added or another hostmask supercedes it.");
		} else if (a >= 50) {
			SENDUSER("You may not add more than 50 hostmasks.");
		} else {
			m_Owner->AddHostAllow(argv[1]);

			SENDUSER("Done.");
		}

		return false;
	} else if (strcmpi(Subcommand, "hostdel") == 0 && argc > 1) {
		if (argc <= 1) {
			SENDUSER("Syntax: HOSTDEL hostmask");

			return false;
		}

		m_Owner->RemoveHostAllow(argv[1]);

		SENDUSER("Done.");

		return false;
	} else if (strcmpi(Subcommand, "admin") == 0 && m_Owner->IsAdmin()) {
		if (argc < 2) {
			SENDUSER("Syntax: ADMIN username");

			return false;
		}

		CBouncerUser* User = g_Bouncer->GetUser(argv[1]);

		if (User) {
			User->SetAdmin(true);

			SENDUSER("Done.");
		} else {
			SENDUSER("There's no such user.");
		}

		return false;
	} else if (strcmpi(Subcommand, "unadmin") == 0 && m_Owner->IsAdmin()) {
		if (argc < 2) {
			SENDUSER("Syntax: UNADMIN username");

			return false;
		}

		CBouncerUser* User = g_Bouncer->GetUser(argv[1]);
		
		if (User) {
			User->SetAdmin(false);

			SENDUSER("Done.");
		} else {
			SENDUSER("There's no such user.");
		}

		return false;
	} else if (strcmpi(Subcommand, "suspend") == 0 && m_Owner->IsAdmin()) {
		if (argc < 2) {
			SENDUSER("Syntax: SUSPEND username :reason");

			return false;
		}

		CBouncerUser* User = g_Bouncer->GetUser(argv[1]);

		if (User) {
			User->Lock();

			if (User->GetClientConnection())
				User->GetClientConnection()->Kill("Your account has been suspended.");

			if (User->GetIRCConnection())
				User->GetIRCConnection()->Kill("Requested.");

			User->MarkQuitted();

			if (argc > 2) {
				ArgRejoinArray(argv, 2);
				User->SetSuspendReason(argv[2]);
			} else {
				User->SetSuspendReason("Suspended.");
			}

			asprintf(&Out, "User %s has been suspended.", User->GetUsername());
			if (Out == NULL) {
				LOGERROR("asprintf() failed.");
			} else {
				g_Bouncer->GlobalNotice(Out, true);
				free(Out);
			}

			SENDUSER("Done.");
		} else {
			SENDUSER("There's no such user.");
		}

		return false;
	} else if (strcmpi(Subcommand, "unsuspend") == 0 && m_Owner->IsAdmin()) {
		if (argc < 2) {
			SENDUSER("Syntax: UNSUSPEND username");

			return false;
		}

		CBouncerUser* User = g_Bouncer->GetUser(argv[1]);
		
		if (User) {
			User->Unlock();

			asprintf(&Out, "User %s has been unsuspended.", User->GetUsername());
			if (Out == NULL) {
				LOGERROR("asprintf() failed.");
			} else {
				g_Bouncer->GlobalNotice(Out, true);
				free(Out);
			}

			User->SetSuspendReason(NULL);

			SENDUSER("Done.");
		} else {
			SENDUSER("There's no such user.");
		}

		return false;
	} else if (strcmpi(Subcommand, "resetpass") == 0 && m_Owner->IsAdmin()) {
		if (argc < 3) {
			SENDUSER("Syntax: RESETPASS username new-password");

			return false;
		}

		CBouncerUser* User = g_Bouncer->GetUser(argv[1]);
		
		if (User) {
			User->SetPassword(argv[2]);

			SENDUSER("Done.");
		} else {
			SENDUSER("There's no such user.");
		}

		return false;
	} else if (strcmpi(Subcommand, "partall") == 0) {
		if (m_Owner->GetIRCConnection()) {
			const char* Channels = m_Owner->GetConfigChannels();

			if (Channels)
				m_Owner->GetIRCConnection()->WriteLine("PART %s", Channels);
		}

		m_Owner->SetConfigChannels(NULL);

		SENDUSER("Done.");

		return false;
	}

	if (NoticeUser)
		m_Owner->RealNotice("Unknown command. Try /sbnc help");
	else
		SENDUSER("Unknown command. Try /msg -sBNC help");

	return false;
}

bool CClientConnection::ParseLineArgV(int argc, const char** argv) {
	char* Out;

	if (argc == 0)
		return false;

	CModule** Modules = g_Bouncer->GetModules();
	int Count = g_Bouncer->GetModuleCount();

	for (int i = 0; i < Count; i++) {
		if (Modules[i]) {
			if (!Modules[i]->InterceptClientMessage(this, argc, argv))
				return false;
		}
	}

	const char* Command = argv[0];

	if (!m_Owner) {
		if (strcmpi(Command, "nick") == 0 && argc > 1) {
			const char* Nick = argv[1];

			if (m_Nick != NULL) {
				if (strcmp(m_Nick, Nick) != 0) {
					if (m_Owner)
						m_Owner->GetConfig()->WriteString("user.nick", Nick);

					WriteLine(":%s!ident@sbnc NICK :%s", m_Nick, Nick);
				}
			}

			free(m_Nick);
			m_Nick = strdup(Nick);

			if (m_Username && m_Password)
				ValidateUser();
			else if (m_Username)
				InternalWriteLine(":Notice!notice@shroudbnc.org NOTICE * :*** This server requires a password. Use /QUOTE PASS thepassword to supply a password now.");
		} else if (strcmpi(Command, "pass") == 0) {
			if (argc < 2) {
				WriteLine(":bouncer 461 %s :Not enough parameters", m_Nick);
			} else {
				m_Password = strdup(argv[1]);
			}

			if (m_Nick && m_Username && m_Password)
				ValidateUser();

			return false;
		} else if (strcmpi(Command, "user") == 0 && argc > 1) {
			if (m_Username) {
				WriteLine(":bouncer 462 %s :You may not reregister", m_Nick);
			} else {
				if (!argv[1]) {
					WriteLine(":bouncer 461 %s :Not enough parameters", m_Nick);
				} else {
					const char* Username = argv[1];

					m_Username = strdup(Username);
				}
			}

			bool ValidSSLCert = false;

			if (m_Nick && m_Username && (m_Password || GetPeerCertificate() != NULL))
				ValidSSLCert = ValidateUser();

			if (m_Nick && m_Username && !m_Password && !ValidSSLCert)
				InternalWriteLine(":Notice!notice@shroudbnc.org NOTICE * :*** This server requires a password. Use /QUOTE PASS thepassword to supply a password now.");

			return false;
		} else if (strcmpi(Command, "quit") == 0) {
			Kill("*** Thanks for flying with shroudBNC :P");
			return false;
		}
	}

	if (m_Owner) {
		if (strcmpi(Command, "quit") == 0) {
			bool QuitAsAway = (m_Owner->GetConfig()->ReadInteger("user.quitaway") != 0);

			if (QuitAsAway && argc > 1 && *argv[1])
				m_Owner->SetAwayText(argv[1]);

			Kill("*** Thanks for flying with shroudBNC :P");
			return false;
		} else if (strcmpi(Command, "nick") == 0) {
			if (argc >= 2) {
				free(m_Nick);
				m_Nick = strdup(argv[1]);
				m_Owner->GetConfig()->WriteString("user.nick", argv[1]);
			}
		} else if (argc > 1 && strcmpi(Command, "join") == 0) {
			CIRCConnection* IRC;
			const char* Key;

			if (argc > 2 && strstr(argv[0], ",") == NULL && strstr(argv[1], ",") == NULL)
				m_Owner->GetKeyring()->AddKey(argv[1], argv[2]);
			else if (m_Owner->GetKeyring() && (Key = m_Owner->GetKeyring()->GetKey(argv[1])) && (IRC = m_Owner->GetIRCConnection())) {
				IRC->WriteLine("JOIN %s %s", argv[1], Key);

				return false;
			}
		} else if (strcmpi(Command, "whois") == 0) {
			if (argc >= 2) {
				const char* Nick = argv[1];

				if (strcmpi("-sbnc", Nick) == 0) {
					WriteLine(":bouncer 311 %s -sBNC core shroudbnc.org * :shroudBNC", m_Nick);
					WriteLine(":bouncer 312 %s -sBNC shroudbnc.org :shroudBNC IRC Proxy", m_Nick);
					WriteLine(":bouncer 318 %s -sBNC :End of /WHOIS list.", m_Nick);

					return false;
				}
			}
		} else if (strcmpi(Command, "perror") == 0) {
			if (argc < 2) {
				m_Owner->Notice("Syntax: PERROR :quit-msg");
				return false;
			}

			CIRCConnection* Conn = m_Owner->GetIRCConnection();

			if (Conn)
				Conn->Kill(argv[1]);

			m_Owner->MarkQuitted();

			return false;
/*		} else if (strcmpi(Command, "simul") == 0 && m_Owner->IsAdmin()) {
			if (argc < 3) {
				m_Owner->Notice("Syntax: SIMUL username :command");
				return false;
			}

			CBouncerUser* User = g_Bouncer->GetUser(argv[1]);

			if (User)
				User->Simulate(argv[2]);
			else {
				asprintf(&Out, "No such user: %s", argv[1]);
				if (Out == NULL) {
					LOGERROR("asprintf() failed.");
				} else {
					m_Owner->Notice(Out);
				}
			}

			return false;*/
		} else if (argc > 2 && strcmpi(Command, "privmsg") == 0 && strcmpi(argv[1], "-sbnc") == 0) {
			const char* Toks = ArgTokenize(argv[2]);
			const char** Arr = ArgToArray(Toks);

			ProcessBncCommand(Arr[0], ArgCount(Toks), Arr, false);

			ArgFreeArray(Arr);
			ArgFree(Toks);

			return false;
		} else if (strcmpi(Command, "sbnc") == 0) {
			return ProcessBncCommand(argv[1], argc - 1, &argv[1], true);
		} else if (strcmpi(Command, "synth") == 0) {
			if (argc < 2) {
				m_Owner->Notice("Syntax: SYNTH command parameter");
				m_Owner->Notice("supported commands are: mode, topic, names, version, who");

				return false;
			}

			if (strcmpi(argv[1], "mode") == 0 && argc > 2) {
				CIRCConnection* IRC = m_Owner->GetIRCConnection();

				if (IRC) {
					CChannel* Chan = IRC->GetChannel(argv[2]);

					if (argc == 3) {
						if (Chan && Chan->AreModesValid()) {
							WriteLine(":%s 324 %s %s %s", IRC->GetServer(), IRC->GetCurrentNick(), argv[2], Chan->GetChanModes());
							WriteLine(":%s 329 %s %s %d", IRC->GetServer(), IRC->GetCurrentNick(), argv[2], Chan->GetCreationTime());
						} else
							IRC->WriteLine("MODE %s", argv[2]);
					} else if (argc == 4 && strcmp(argv[3],"+b") == 0) {
						if (Chan && Chan->HasBans()) {
							CBanlist* Bans = Chan->GetBanlist();

							int i = 0; 

							while (const ban_t* Ban = Bans->Iterate(i++)) {
								WriteLine(":%s 367 %s %s %s %s %d", IRC->GetServer(), IRC->GetCurrentNick(), argv[2], Ban->Mask, Ban->Nick, Ban->TS);
							}

							WriteLine(":%s 368 %s %s :End of Channel Ban List", IRC->GetServer(), IRC->GetCurrentNick(), argv[2]);
						} else
							IRC->WriteLine("MODE %s +b", argv[2]);
					}
				}
			} else if (strcmpi(argv[1], "topic") == 0 && argc > 2) {
				CIRCConnection* IRC = m_Owner->GetIRCConnection();

				if (IRC) {
					CChannel* Chan = IRC->GetChannel(argv[2]);

					if (Chan && Chan->HasTopic() != 0) {
						if (Chan->GetTopic() && *(Chan->GetTopic())) {
							WriteLine(":%s 332 %s %s :%s", IRC->GetServer(), IRC->GetCurrentNick(), argv[2], Chan->GetTopic());
							WriteLine(":%s 333 %s %s %s %d", IRC->GetServer(), IRC->GetCurrentNick(), argv[2], Chan->GetTopicNick(), Chan->GetTopicStamp());
						}
					} else {
						IRC->WriteLine("TOPIC %s", argv[2]);
					}
				}
			} else if (strcmpi(argv[1], "names") == 0 && argc > 2) {
				CIRCConnection* IRC = m_Owner->GetIRCConnection();

				if (IRC) {
					CChannel* Chan = IRC->GetChannel(argv[2]);

					if (Chan && Chan->HasNames() != 0) {
						char* Nicks = (char*)malloc(1);
						Nicks[0] = '\0';

						CHashtable<CNick*, false, 64>* H = Chan->GetNames();

						int a = 0;

						while (xhash_t<CNick*>* NickHash = H->Iterate(a++)) {
							CNick* NickObj = NickHash->Value;

							const char* Prefix = NickObj->GetPrefixes();
							const char* Nick = NickObj->GetNick();

							char outPref[2] = { Chan->GetHighestUserFlag(Prefix), '\0' };

							if (Nick == NULL)
								continue;

							Nicks = (char*)realloc(Nicks, (Nicks ? strlen(Nicks) : 0) + strlen(outPref) + strlen(Nick) + 2);

							if (Nicks == NULL) {
								Kill("CClientConnection::ParseLineArgV: realloc() failed. Please reconnect.");

								return false;
							}

							if (*Nicks)
								strcat(Nicks, " ");

							strcat(Nicks, outPref);
							strcat(Nicks, Nick);

							if (strlen(Nicks) > 400) {
								WriteLine(":%s 353 %s = %s :%s", IRC->GetServer(), IRC->GetCurrentNick(), argv[2], Nicks);

								Nicks = (char*)realloc(Nicks, 1);

								if (Nicks == NULL) {
									Kill("CClientConnection::ParseLineArgV: realloc() failed. Please reconnect.");

									return false;
								}

								*Nicks = '\0';
							}
						}

						if (a) {
							WriteLine(":%s 353 %s = %s :%s", IRC->GetServer(), IRC->GetCurrentNick(), argv[2], Nicks);
						}

						free(Nicks);

						WriteLine(":%s 366 %s %s :End of /NAMES list.", IRC->GetServer(), IRC->GetCurrentNick(), argv[2]);
					} else {
						IRC->WriteLine("NAMES %s", argv[2]);
					}
				}
			} else if (strcmpi(argv[1], "who") == 0) {
				CIRCConnection* IRC = m_Owner->GetIRCConnection();

				if (IRC) {
					CChannel *Channel = IRC->GetChannel(argv[2]);

					if (Channel && time(NULL) - m_Owner->GetLastSeen() < 300 && Channel->SendWhoReply(true)) {
						Channel->SendWhoReply(false);
					} else {
						IRC->WriteLine("WHO %s", argv[2]);
					}
				}
			} else if (strcmpi(argv[1], "version") == 0) {
				CIRCConnection* IRC = m_Owner->GetIRCConnection();

				if (IRC) {
					const char* ServerVersion = IRC->GetServerVersion();
					const char* ServerFeat = IRC->GetServerFeat();

					if (ServerVersion != NULL && ServerFeat != NULL) {
						WriteLine(":%s 351 %s %s %s :%s", IRC->GetServer(), IRC->GetCurrentNick(), ServerVersion, IRC->GetServer(), ServerFeat);
					}

					char* Feats = (char*)malloc(1);
					Feats[0] = '\0';

					int a = 0, i = 0;

					while (xhash_t<char*>* Feat = IRC->GetISupportAll()->Iterate(i++)) {
						char* Name = Feat->Name;
						char* Value = Feat->Value;

						Feats = (char*)realloc(Feats, (Feats ? strlen(Feats) : 0) + strlen(Name) + 1 + strlen(Value) + 2);

						if (Feats == NULL) {
							Kill("CClientConnection::ParseLineArgV: realloc() failed. Please reconnect.");

							return false;
						}


						if (*Feats)
							strcat(Feats, " ");

						strcat(Feats, Name);

						if (Value && *Value) {
							strcat(Feats, "=");
							strcat(Feats, Value);
						}

						if (++a == 11) {
							WriteLine(":%s 005 %s %s :are supported by this server", IRC->GetServer(), IRC->GetCurrentNick(), Feats);

							Feats = (char*)realloc(Feats, 1);

							if (Feats == NULL) {
								Kill("CClientConnection::ParseLineArgV: realloc() failed. Please reconnect.");

								return false;
					
							}

							*Feats = '\0';
							a = 0;
						}
					}

					if (a)
						WriteLine(":%s 005 %s %s :are supported by this server", IRC->GetServer(), IRC->GetCurrentNick(), Feats);

					free(Feats);
				}
			}

			return false;
		} else if (strcmpi(Command, "mode") == 0 || strcmpi(Command, "topic") == 0 ||
				strcmpi(Command, "names") == 0 || strcmpi(Command, "who") == 0) {
			if (argc == 2 || (strcmpi(Command, "mode") == 0 && argc == 3) && strcmp(argv[2],"+b") == 0) {
				if (argc == 2)
					asprintf(&Out, "SYNTH %s %s", argv[0], argv[1]);
				else
					asprintf(&Out, "SYNTH %s %s %s", argv[0], argv[1], argv[2]);

				if (Out == NULL) {
					LOGERROR("asprintf() failed.");
				} else {
					ParseLine(Out);
					free(Out);
				}

				return false;
			}
		} else if (strcmpi(Command, "version") == 0) {
			ParseLine("SYNTH VERSION");

			return false;
		}
	}

	return true;
}

void CClientConnection::ParseLine(const char* Line) {
	if (strlen(Line) > 512)
		return; // protocol violation

	const char* Args;
	const char** argv;
	int argc;

	Args = ArgTokenize(Line);

	if (Args == NULL) {
		LOGERROR("ArgTokenize() failed (%s).", Line);

		return;
	}

	argv = ArgToArray(Args);

	if (argv == NULL) {
		LOGERROR("ArgToArray() failed (%s).", Line);

		ArgFree(Args);

		return;
	}

	argc = ArgCount(Args);

	bool Ret = ParseLineArgV(argc, argv);

	ArgFreeArray(argv);
	ArgFree(Args);

	if (m_Owner && Ret) {
		CIRCConnection* IRC = m_Owner->GetIRCConnection();

		if (IRC)
			IRC->InternalWriteLine(Line);
	}
}

bool CClientConnection::ValidateUser() {
	bool Force = false;
	CBouncerUser* User;

	bool Blocked = true, Valid = false, ValidHost = false;

#ifdef USESSL
	int Count = 0;
	bool MatchUsername = false;
	X509* PeerCert = NULL;
	CBouncerUser* AuthUser = NULL;

	if (IsSSL() && (PeerCert = (X509*)GetPeerCertificate()) != NULL) {
		int i = 0;

		while (xhash_t<CBouncerUser *> *UserHash = g_Bouncer->GetUsers()->Iterate(i++)) {
			if (UserHash->Value->FindClientCertificate(PeerCert)) {
				AuthUser = UserHash->Value;
				Count++;

				if (strcmpi(UserHash->Name, m_Username) == 0)
					MatchUsername = true;
			}
		}

		if (AuthUser && Count == 1) { // found a single user who has this public certificate
			free(m_Username);
			m_Username = strdup(AuthUser->GetUsername());
			Force = true;
		} else if (MatchUsername == true && Count > 1) // found more than one user with that certificate
			Force = true;
	}

	if (AuthUser == NULL && m_Password == NULL)
		return false;
#endif

	User = g_Bouncer->GetUser(m_Username);

	if (User) {
		Blocked = User->IsIpBlocked(m_Peer);
		Valid = (Force || User->Validate(m_Password));
		ValidHost = User->CanHostConnect(m_PeerName);
	}

	if ((m_Password || Force) && User && !Blocked && Valid && ValidHost) {
		User->Attach(this);
		//WriteLine(":Notice!notice@shroudbnc.org NOTICE * :Welcome to the wonderful world of IRC");
	} else {
		if (User && !Blocked) {
			User->LogBadLogin(m_Peer);
		}

		if (User && !ValidHost && !Blocked) {
			g_Bouncer->Log("Attempted login from %s for %s denied: Host does not match any host allows.", inet_ntoa(m_Peer.sin_addr), m_Username);
		} else if (User && Blocked) {
			g_Bouncer->Log("Blocked login attempt from %s for %s", inet_ntoa(m_Peer.sin_addr), m_Username);
		} else if (User) {
			g_Bouncer->Log("Wrong password for user %s", m_Username);
		}

		Kill("*** Unknown user or wrong password.");
	}

	return true;
}


const char* CClientConnection::GetNick(void) {
	return m_Nick;
}

void CClientConnection::Destroy(void) {
	if (m_Owner) {
		g_Bouncer->Log("%s disconnected.", m_Username);
		m_Owner->SetClientConnection(NULL);
	}

	delete this;
}

void CClientConnection::SetOwner(CBouncerUser* Owner) {
	m_Owner = Owner;
}

void CClientConnection::SetPeerName(const char* PeerName, bool LookupFailure) {
	if (!LookupFailure)
		WriteLine(":Notice!notice@shroudbnc.org NOTICE * :*** Found your hostname (%s)", PeerName);
	else
		WriteLine(":Notice!notice@shroudbnc.org NOTICE * :*** Failed to resolve your host. Using IP address instead (%s)", PeerName);

	m_PeerName = strdup(PeerName);

	ReadLines();
}

adns_query CClientConnection::GetPeerDNSQuery(void) {
	return m_PeerA;
}

sockaddr_in CClientConnection::GetPeer(void) {
	return m_Peer;
}

const char* CClientConnection::GetPeerName(void) {
	return m_PeerName;
}

void CClientConnection::AsyncDnsFinishedClient(adns_query* query, adns_answer* response) {
	m_DnsEvents = NULL;

	if (m_AdnsTimeout) {
		m_AdnsTimeout->Destroy();
		m_AdnsTimeout = NULL;
	}

	if (!response || response->status != adns_s_ok)
		SetPeerName(inet_ntoa(GetPeer().sin_addr), true);
	else
		SetPeerName(*response->rrs.str, false);
}

const char* CClientConnection::ClassName(void) {
	return "CClientConnection";
}

bool CClientConnection::Read(bool DontProcess) {
	bool Ret;

	if (m_PeerName)
		Ret = CConnection::Read(false);
	else
		return CConnection::Read(true);

	if (Ret && RecvqSize() > 5120) {
		Kill("RecvQ exceeded.");
	}

	return Ret;
}

void CClientConnection::AdnsTimeout(void) {
	m_AdnsTimeout = NULL;

	if (!m_PeerName && m_Socket != INVALID_SOCKET)
		adns_cancel(m_PeerA);
	
	SetPeerName(inet_ntoa(GetPeer().sin_addr), true);
}

bool AdnsTimeoutTimer(time_t Now, void* Client) {
	((CClientConnection*)Client)->AdnsTimeout();

	return false;
}

void CClientConnection::InternalWriteLine(const char* In) {
	CConnection::InternalWriteLine(In);

	if (m_Owner && !m_Owner->IsAdmin() && SendqSize() > g_Bouncer->GetSendQSize() * 1024) {
		FlushSendQ();
		CConnection::InternalWriteLine("");
		Kill("SendQ exceeded.");
	}
}

bool CClientConnection::Freeze(CAssocArray *Box) {
	// too bad we can't preserve ssl encrypted connections
	if (m_AdnsTimeout || m_PeerName == NULL || GetSocket() == INVALID_SOCKET || IsSSL())
		return false;

	Box->AddString("client.peername", m_PeerName);
	Box->AddString("client.nick", m_Nick);
	Box->AddInteger("client.fd", GetSocket());

	// protect the socket from being closed
	g_Bouncer->UnregisterSocket(m_Socket);
	m_Socket = INVALID_SOCKET;

	Destroy();

	return true;
}

void CClientConnection::Kill(const char *Error) {
	if (m_Owner) {
		m_Owner->SetClientConnection(NULL);
		m_Owner = NULL;
	}

	WriteLine(":Notice!notice!shroudbnc.org NOTICE * :%s", Error);

	CConnection::Kill(Error);
}

commandlist_t *CClientConnection::GetCommandList(void) {
	return &m_CommandList;
}