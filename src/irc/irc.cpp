/*
 * Copyright(C) 2009 Romain Bignon
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <sys/socket.h>
#include <fcntl.h>
#include <netdb.h>
#include <cstring>

#include "../log.h"
#include "../util.h"
#include "../callback.h"
#include "../sock.h"
#include "../server_poll/poll.h"
#include "irc.h"
#include "message.h"
#include "user.h"
#include "rootnick.h"
#include "channel.h"

static struct
{
	const char* cmd;
	void (IRC::*func)(Message);
	size_t minargs;
} commands[] = {
	{ "NICK",    &IRC::m_nick,    0 },
	{ "USER",    &IRC::m_user,    4 },
	{ "QUIT",    &IRC::m_quit,    0 },
	{ "PRIVMSG", &IRC::m_privmsg, 2 },
};

IRC::IRC(ServerPoll* _poll, int _fd, string _hostname, string cmd_chan_name)
	: poll(_poll),
	  fd(_fd),
	  read_id(0),
	  read_cb(NULL),
	  hostname("localhost.localdomain"),
	  user(NULL),
	  rootNick(NULL),
	  cmdChan(NULL)
{
	struct sockaddr_storage sock;
	socklen_t socklen = sizeof(sock);

	fcntl(0, F_SETFL, O_NONBLOCK);

	/* Get the user's hostname. */
	string userhost = "localhost.localdomain";
	if(getpeername(fd, (struct sockaddr*) &sock, &socklen) == 0)
	{
		char buf[NI_MAXHOST+1];

		if(getnameinfo((struct sockaddr *)&sock, socklen, buf, NI_MAXHOST, NULL, 0, 0) == 0)
			userhost = buf;
	}

	/* Get my own hostname (if not given in arguments) */
	if(_hostname.empty() || _hostname == " ")
	{
		if(getsockname(fd, (struct sockaddr*) &sock, &socklen) == 0)
		{
			char buf[NI_MAXHOST+1];

			if(getnameinfo((struct sockaddr *) &sock, socklen, buf, NI_MAXHOST, NULL, 0, 0 ) == 0)
				hostname = buf;
		}
	}
	else if(_hostname.find(" ") != string::npos)
	{
		/* An hostname can't contain any space. */
		b_log[W_ERR] << "'" << _hostname << "' is not a valid server hostname";
		throw IRCAuthError();
	}
	else
		hostname = _hostname;

	if(Channel::isChanName(cmd_chan_name))
	{
		b_log[W_ERR] << "'" << cmd_chan_name << "' is not a valid command channel name";
		throw IRCAuthError();
	}


	/* create a callback on the sock. */
	read_cb = new CallBack<IRC>(this, &IRC::readIO);
	read_id = glib_input_add(fd, (PurpleInputCondition)PURPLE_INPUT_READ, g_callback_input, read_cb);

	/* Create main objects and root joins command channel. */
	user = new User(fd, "*", "", userhost);
	rootNick = new RootNick(hostname);
	addNick(rootNick);
	addNick(user);
	cmdChan = new Channel(this, cmd_chan_name);
	addChannel(cmdChan);
	rootNick->join(cmdChan, ChanUser::OP);

	user->send(Message(MSG_NOTICE).setSender(this).setReceiver("AUTH").addArg("BitlBee-IRCd initialized, please go on"));
}

IRC::~IRC()
{
	if(read_id > 0)
		g_source_remove(read_id);
	delete read_cb;
	delete user;
	delete rootNick;
	delete cmdChan;
}

void IRC::addChannel(Channel* chan)
{
	channels[chan->getName()] = chan;
}
Channel* IRC::getChannel(string channame) const
{
	map<string, Channel*>::const_iterator it = channels.find(channame);
	if(it == channels.end())
		return 0;

	return it->second;
}
void IRC::removeChannel(string channame)
{
	map<string, Channel*>::iterator it = channels.find(channame);
	if(it != channels.end())
	{
		delete it->second;
		channels.erase(it);
	}
}

void IRC::addNick(Nick* nick)
{
	users[nick->getNickname()] = nick;
}

Nick* IRC::getNick(string nickname) const
{
	map<string, Nick*>::const_iterator it = users.find(nickname);
	if(it == users.end())
		return 0;

	return it->second;
}
void IRC::removeNick(string nickname)
{
	map<string, Nick*>::iterator it = users.find(nickname);
	if(it != users.end())
	{
		delete it->second;
		users.erase(it);
	}
}

void IRC::quit(string reason)
{
	user->send(Message(MSG_ERROR).addArg("Closing Link: " + reason));
	close(fd);
	poll->kill(this);
}

void IRC::sendWelcome()
{
	if(user->hasFlag(Nick::REGISTERED) || user->getNickname() == "*" ||
	   user->getIdentname().empty())
		return;

	user->setFlag(Nick::REGISTERED);

	user->send(Message(RPL_WELCOME).setSender(this).setReceiver(user).addArg("Welcome to the BitlBee gateway, " + user->getNickname() + "!"));
	user->send(Message(RPL_YOURHOST).setSender(this).setReceiver(user).addArg("Host " + hostname + " is running BitlBee 2.0"));

	user->join(cmdChan, ChanUser::OP);
	rootNick->privmsg(cmdChan, "Welcome to Bitlbee, dear!");
}

void IRC::readIO(void*)
{
	static char buf[1024];
	string sbuf, line;
	ssize_t r;

	if((r = read( 0, buf, sizeof buf - 1 )) <= 0)
	{
		if(r == 0)
			this->quit("Connection reset by peer...");
		else if(!sockerr_again())
			this->quit(string("Read error: ") + strerror(errno));
		return;
	}
	buf[r] = 0;
	sbuf = buf;

	while((line = stringtok(sbuf, "\r\n")).empty() == false)
	{
		Message m = Message::parse(line);
		size_t i;
		for(i = 0;
		    i < (sizeof commands / sizeof *commands) &&
		    strcmp(commands[i].cmd, m.getCommand().c_str());
		    ++i)
			;

		if(i >= (sizeof commands / sizeof *commands))
			user->send(Message(ERR_UNKNOWNCOMMAND).setSender(this)
			                                   .setReceiver(user)
							   .addArg(m.getCommand())
							   .addArg("Unknown command"));
		else if(m.countArgs() < commands[i].minargs)
			user->send(Message(ERR_NEEDMOREPARAMS).setSender(this)
							   .setReceiver(user)
							   .addArg(m.getCommand())
							   .addArg("Not enough parameters"));
		else
			(this->*commands[i].func)(m);
	}
}

/* NICK nickname */
void IRC::m_nick(Message message)
{
	if(message.countArgs() < 1)
	{
		user->send(Message(ERR_NONICKNAMEGIVEN).setSender(this)
		                                    .setReceiver(user)
						    .addArg("No nickname given"));
		return;
	}
	if(user->hasFlag(Nick::REGISTERED))
		user->send(Message(MSG_NICK).setSender(user).setReceiver(message.getArg(0)));

	user->setNickname(message.getArg(0));

	sendWelcome();
}

/* USER identname * * :realname*/
void IRC::m_user(Message message)
{
	if(user->hasFlag(Nick::REGISTERED))
	{
		user->send(Message(ERR_ALREADYREGISTRED).setSender(this)
		                                     .setReceiver(user)
						     .addArg("Please register only once per session"));
		return;
	}
	user->setIdentname(message.getArg(0));
	user->setRealname(message.getArg(3));

	sendWelcome();
}

/* QUIT [message] */
void IRC::m_quit(Message message)
{
	string reason = "Leaving...";
	if(message.countArgs() >= 1)
		reason = message.getArg(0);
	quit(reason);
}

/* PRIVMSG target message */
void IRC::m_privmsg(Message message)
{
	Message relayed(message.getCommand());
	relayed.setSender(user);
	relayed.setReceiver(message.getArg(0));
	relayed.addArg(message.getArg(1));

	if(Channel::isChanName(relayed.getReceiver()))
	{
		Channel* c = getChannel(relayed.getReceiver());
		if(!c)
		{
			user->send(Message(ERR_NOSUCHCHANNEL).setSender(this)
					                     .setReceiver(user)
							     .addArg(relayed.getReceiver())
							     .addArg("No suck channel"));
			return;
		}
		c->broadcast(relayed, user);
	}
	else
	{
		Nick* n = getNick(relayed.getReceiver());
		if(!n)
		{
			user->send(Message(ERR_NOSUCHNICK).setSender(this)
					                  .setReceiver(user)
							  .addArg(relayed.getReceiver())
							  .addArg("No suck nick"));
			return;
		}
		n->send(relayed);
	}
}
