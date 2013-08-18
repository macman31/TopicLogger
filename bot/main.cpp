/**
 * main.cpp - TopicLogger
 */

#include <yaml-cpp/yaml.h>
#include <libircclient.h>
#include <libirc_rfcnumeric.h>
#include <vector>
#include <my_global.h>
#include <mysql.h>
#include <map>
#include <cstring>
#include <set>

#define DB_COL_TYPE 1
#define DB_COL_TIMESTAMP 2
#define DB_COL_WHO 3
#define DB_COL_RAW_NICK 4
#define DB_COL_CHANNEL 5
#define DB_COL_BODY 6

typedef struct
{
	std::set<std::string>* users;
	std::string* topic;
} irc_room_t;

typedef struct
{
	MYSQL* dbcon;
	YAML::Node config;
	std::map<std::string,irc_room_t*> channels;
	std::string* nick;
} irc_ctx_t;

char* _(const char* str)
{
	char* res = new char[strlen(str)+1];
	strcpy(res, str);
	return res;
}

char* s(MYSQL* con, const char* to)
{
	char* res = new char[2*strlen(to)+1];
	mysql_real_escape_string(con, res, to, strlen(to));
	return res;
}

char* stripnick(const char* origin)
{
	char* res = new char[128];
	irc_target_get_nick(origin, res, 128);
	return res;
}

std::string stripstatus(std::string nick)
{
	switch (nick[0])
	{
		case '+':
		case '%':
		case '@':
		case '!':
		case '&':
		case '~':
			return stripstatus(nick.substr(1));
		default:
			return nick;
	}
}

void event_connect(irc_session_t* session, const char* event, const char* origin, const char** params, unsigned int count)
{
	printf("Connected to IRC\n");
	
	irc_ctx_t* ctx = (irc_ctx_t*) irc_get_ctx(session);

	// Identify with NickServ
	if (!ctx->config["irc_password"].IsNull())
	{
		std::string password = ctx->config["irc_password"].as<std::string>();
		irc_cmd_msg(session, "NickServ", (std::string("IDENTIFY ") + password).c_str());
	}
	
	// Join channels
	std::vector<std::string> channels = ctx->config["irc_channels"].as<std::vector<std::string> >();
	for (std::vector<std::string>::iterator it = channels.begin(); it != channels.end(); ++it)
	{
		irc_cmd_join(session, it->c_str(), 0);
	}
}

void event_numeric(irc_session_t* session, unsigned int event, const char* origin, const char** params, unsigned int count)
{
	irc_ctx_t* ctx = (irc_ctx_t*) irc_get_ctx(session);
	
	if (event > 400)
	{
		std::string fulltext;
		for (unsigned int i = 0; i < count; i++)
		{
			if (i > 0)
			{
				fulltext += " ";
			}

			fulltext += params[i];
		}

		printf("ERROR %d: %s: %s\n", event, origin ? origin : "?", fulltext.c_str());
	} else if (event == LIBIRC_RFC_RPL_NAMREPLY)
	{
		// Userlist -- remember all users in all channels except for self
		irc_room_t* room = ctx->channels[std::string(params[2])];
		
		std::string userlist(params[3]);
		while (userlist.find(" ") != std::string::npos)
		{
			int pos = userlist.find(" ");
			std::string nick = stripstatus(userlist.substr(0, pos));
			
			if (ctx->nick->compare(nick))
			{
				room->users->insert(nick);
			}
			
			userlist = userlist.substr(pos+1);
		}
		
		if (ctx->nick->compare(userlist))
		{
			room->users->insert(stripstatus(userlist));
		}
	}
}

void event_nick(irc_session_t* session, const char* event, const char* origin, const char** params, unsigned int count)
{
	irc_ctx_t* ctx = (irc_ctx_t*) irc_get_ctx(session);
	
	if (!ctx->nick->compare(std::string(stripnick(origin))))
	{
		// Our own nick changed
		ctx->nick = new std::string(params[0]);
	} else {
		// Log nick change in all applicable channels
		for (std::map<std::string,irc_room_t*>::iterator it = ctx->channels.begin(); it != ctx->channels.end(); ++it)
		{
			irc_room_t* room = it->second;
		
			// Check if user is in channel
			std::set<std::string>::iterator cuser = room->users->find(stripnick(origin));
			if (cuser != room->users->end())
			{
				room->users->erase(cuser);
				room->users->insert(params[0]);
			
				char* stmt = (char*) malloc(1024*sizeof(char));
				sprintf(stmt, "INSERT INTO messages (type,who,raw_nick,channel,body) VALUES (\"nick\",\"%s\",\"%s\",\"%s\",\"%s\")", s(ctx->dbcon, stripnick(origin)), s(ctx->dbcon, origin), s(ctx->dbcon, it->first.c_str()), s(ctx->dbcon, params[0]));
				if (mysql_query(ctx->dbcon, stmt))
				{
					fprintf(stderr, "%s\n", mysql_error(ctx->dbcon));
					mysql_close(ctx->dbcon);
					exit(1);
				}
		
				free(stmt);
			}
		}
	}
}

void event_quit(irc_session_t* session, const char* event, const char* origin, const char** params, unsigned int count)
{
	// Log quit in all applicable channels
	irc_ctx_t* ctx = (irc_ctx_t*) irc_get_ctx(session);
	
	for (std::map<std::string,irc_room_t*>::iterator it = ctx->channels.begin(); it != ctx->channels.end(); ++it)
	{
		irc_room_t* room = it->second;
		
		// Check if user is in channel
		std::set<std::string>::iterator cuser = room->users->find(stripnick(origin));
		if (cuser != room->users->end())
		{
			room->users->erase(cuser);
			
			const char* reason;
			if (count == 2)
			{
				reason = params[1];
			} else {
				reason = "";
			}
			
			char* stmt = (char*) malloc(1280*sizeof(char));
			sprintf(stmt, "INSERT INTO messages (type,who,raw_nick,channel,body) VALUES (\"quit\",\"%s\",\"%s\",\"%s\",\"%s\")", s(ctx->dbcon, stripnick(origin)), s(ctx->dbcon, origin), s(ctx->dbcon, it->first.c_str()), s(ctx->dbcon, reason));
			if (mysql_query(ctx->dbcon, stmt))
			{
				fprintf(stderr, "%s\n", mysql_error(ctx->dbcon));
				mysql_close(ctx->dbcon);
				exit(1);
			}
		
			free(stmt);
		}
	}
}

void event_join(irc_session_t* session, const char* event, const char* origin, const char** params, unsigned int count)
{
	irc_ctx_t* ctx = (irc_ctx_t*) irc_get_ctx(session);
	if (!ctx->nick->compare(std::string(stripnick(origin))))
	{
		// If the bot is the one joining, initialize the room
		irc_room_t* nroom = (irc_room_t*) malloc(sizeof(irc_room_t));
		nroom->topic = 0;
		nroom->users = new std::set<std::string>();
		
		// Load last topic from database
		char* stmt = (char*) malloc(512*sizeof(char));
		sprintf(stmt, "SELECT * FROM messages WHERE channel = \"%s\" AND type = \"subject\" ORDER BY timestamp DESC LIMIT 1", s(ctx->dbcon, params[0]));
		if (mysql_query(ctx->dbcon, stmt))
		{
			fprintf(stderr, "%s\n", mysql_error(ctx->dbcon));
			mysql_close(ctx->dbcon);
			exit(1);
		}
		
		MYSQL_RES* result = mysql_store_result(ctx->dbcon);
		if (result == NULL)
		{
			fprintf(stderr, "%s\n", mysql_error(ctx->dbcon));
			mysql_close(ctx->dbcon);
			exit(1);
		}
		
		MYSQL_ROW row;
		while (row = mysql_fetch_row(result))
		{
			nroom->topic = new std::string(row[DB_COL_BODY]);
		}
		
		mysql_free_result(result);
		free(stmt);
		
		ctx->channels[params[0]] = nroom;
	} else {
		// Add user to channel's userlist
		irc_room_t* room = ctx->channels[std::string(params[0])];
		room->users->insert(std::string(stripnick(origin)));
		
		// Log the join
		char* stmt = (char*) malloc(768*sizeof(char));
		sprintf(stmt, "INSERT INTO messages (type,who,raw_nick,channel,body) VALUES (\"join\",\"%s\",\"%s\",\"%s\",\"\")", s(ctx->dbcon, stripnick(origin)), s(ctx->dbcon, origin), s(ctx->dbcon, params[0]));
		if (mysql_query(ctx->dbcon, stmt))
		{
			fprintf(stderr, "%s\n", mysql_error(ctx->dbcon));
			mysql_close(ctx->dbcon);
			exit(1);
		}
		
		free(stmt);
		
		// Send the new user a notice
		char* msg = new char[512];
		sprintf(msg, "Welcome to %s, %s. The current topic of discussion is \"%s\".\n", params[0], stripnick(origin), room->topic->c_str());
		irc_cmd_notice(session, stripnick(origin), msg);
		free(msg);
	}
}

void event_part(irc_session_t* session, const char* event, const char* origin, const char** params, unsigned int count)
{
	// Log part, unless we parted
	irc_ctx_t* ctx = (irc_ctx_t*) irc_get_ctx(session);
	if (!ctx->nick->compare(std::string(stripnick(origin))))
	{
		// Remove the user from the channel's userlist
		irc_room_t* room = ctx->channels[std::string(params[0])];
		room->users->erase(room->users->find(std::string(stripnick(origin))));
		
		// Log the actual part
		const char* reason;
		if (count == 2)
		{
			reason = params[1];
		} else {
			reason = "";
		}
		
		char* stmt = (char*) malloc(1280*sizeof(char));
		sprintf(stmt, "INSERT INTO messages (type,who,raw_nick,channel,body) VALUES (\"part\",\"%s\",\"%s\",\"%s\",\"%s\")", s(ctx->dbcon, stripnick(origin)), s(ctx->dbcon, origin), s(ctx->dbcon, params[0]), s(ctx->dbcon, reason));
		if (mysql_query(ctx->dbcon, stmt))
		{
			fprintf(stderr, "%s\n", mysql_error(ctx->dbcon));
			mysql_close(ctx->dbcon);
			exit(1);
		}
		
		free(stmt);
	}
}

void event_mode(irc_session_t* session, const char* event, const char* origin, const char** params, unsigned int count)
{
	// Log the mode change
	irc_ctx_t* ctx = (irc_ctx_t*) irc_get_ctx(session);
	
	std::string fulltext;
	for (unsigned int i = 1; i < count; i++)
	{
		if (i > 1)
		{
			fulltext += " ";
		}

		fulltext += params[i];
	}
	
	char* stmt = (char*) malloc(1280*sizeof(char));
	sprintf(stmt, "INSERT INTO messages (type,who,raw_nick,channel,body) VALUES (\"cmode\",\"%s\",\"%s\",\"%s\",\"%s\")", s(ctx->dbcon, stripnick(origin)), s(ctx->dbcon, origin), s(ctx->dbcon, params[0]), s(ctx->dbcon, fulltext.c_str()));
	if (mysql_query(ctx->dbcon, stmt))
	{
		fprintf(stderr, "%s\n", mysql_error(ctx->dbcon));
		mysql_close(ctx->dbcon);
		exit(1);
	}
	
	free(stmt);
}

void event_kick(irc_session_t* session, const char* event, const char* origin, const char** params, unsigned int count)
{
	// Log the kick unless we just got kicked
	irc_ctx_t* ctx = (irc_ctx_t*) irc_get_ctx(session);
	if (!ctx->nick->compare(std::string(stripnick(params[1]))))
	{
		const char* reason;
		if (count == 3)
		{
			reason = (std::string(params[1]) + std::string(" (") + std::string(params[2]) + std::string(")")).c_str();
		} else {
			reason = params[1];
		}
		
		char* stmt = (char*) malloc(1280*sizeof(char));
		sprintf(stmt, "INSERT INTO messages (type,who,raw_nick,channel,body) VALUES (\"kick\",\"%s\",\"%s\",\"%s\",\"%s\")", s(ctx->dbcon, stripnick(origin)), s(ctx->dbcon, origin), s(ctx->dbcon, params[0]), s(ctx->dbcon, reason));
		if (mysql_query(ctx->dbcon, stmt))
		{
			fprintf(stderr, "%s\n", mysql_error(ctx->dbcon));
			mysql_close(ctx->dbcon);
			exit(1);
		}
		
		free(stmt);
	}
}

void event_topic(irc_session_t* session, const char* event, const char* origin, const char** params, unsigned int count)
{
	// Make sure there's actually a message attached
	if (count < 2)
	{
		return;
	}
	
	// Log topic
	irc_ctx_t* ctx = (irc_ctx_t*) irc_get_ctx(session);
	
	char* stmt = (char*) malloc(1280*sizeof(char));
	sprintf(stmt, "INSERT INTO messages (type,who,raw_nick,channel,body) VALUES (\"topic\",\"%s\",\"%s\",\"%s\",\"%s\")", s(ctx->dbcon, stripnick(origin)), s(ctx->dbcon, origin), s(ctx->dbcon, params[0]), s(ctx->dbcon, params[1]));
	if (mysql_query(ctx->dbcon, stmt))
	{
		fprintf(stderr, "%s\n", mysql_error(ctx->dbcon));
		mysql_close(ctx->dbcon);
		exit(1);
	}
	
	free(stmt);
}

void event_channel(irc_session_t* session, const char* event, const char* origin, const char** params, unsigned int count)
{
	// Make sure there's actually a message attached
	if (count < 2)
	{
		return;
	}
	
	irc_ctx_t* ctx = (irc_ctx_t*) irc_get_ctx(session);
	
	std::string msg(params[1]);
	if (!msg.substr(0, 6).compare(std::string("!topic")))
	{
		if (msg.length() <= 7)
		{
			// Print current topic
			irc_room_t* room = ctx->channels[std::string(params[0])];
		
			if (!room->topic)
			{
				irc_cmd_msg(session, params[0], "There is no topic currently set");
			} else {
				irc_cmd_msg(session, params[0], (std::string("The current topic is \"") + *room->topic + "\"").c_str());
			}
		} else {
			// Change topic
			irc_room_t* room = ctx->channels[std::string(params[0])];
			delete room->topic;
			room->topic = new std::string(msg.substr(7));
			irc_cmd_msg(session, params[0], (std::string("The topic has been changed to \"") + *room->topic + "\"").c_str());
		
			char* stmt = (char*) malloc(1280*sizeof(char));
			sprintf(stmt, "INSERT INTO messages (type,who,raw_nick,channel,body) VALUES (\"subject\",\"%s\",\"%s\",\"%s\",\"%s\")", s(ctx->dbcon, stripnick(origin)), s(ctx->dbcon, origin), s(ctx->dbcon, params[0]), s(ctx->dbcon, room->topic->c_str()));
			if (mysql_query(ctx->dbcon, stmt))
			{
				fprintf(stderr, "%s\n", mysql_error(ctx->dbcon));
				mysql_close(ctx->dbcon);
				exit(1);
			}
		
			free(stmt);
		}
	} else if (!msg.compare(std::string("!log")))
	{
		// NOTICE the user a link to the logs
	} else {
		// Log message
		char* stmt = (char*) malloc(1280*sizeof(char));
		sprintf(stmt, "INSERT INTO messages (type,who,raw_nick,channel,body) VALUES (\"privmsg\",\"%s\",\"%s\",\"%s\",\"%s\")", s(ctx->dbcon, stripnick(origin)), s(ctx->dbcon, origin), s(ctx->dbcon, params[0]), s(ctx->dbcon, params[1]));
		if (mysql_query(ctx->dbcon, stmt))
		{
			fprintf(stderr, "%s\n", mysql_error(ctx->dbcon));
			mysql_close(ctx->dbcon);
			exit(1);
		}
		
		free(stmt);
	}
}

void event_ctcp_action(irc_session_t* session, const char* event, const char* origin, const char** params, unsigned int count)
{
	// Log action
	irc_ctx_t* ctx = (irc_ctx_t*) irc_get_ctx(session);
	
	char* stmt = (char*) malloc(1280*sizeof(char));
	sprintf(stmt, "INSERT INTO messages (type,who,raw_nick,channel,body) VALUES (\"action\",\"%s\",\"%s\",\"%s\",\"%s\")", s(ctx->dbcon, stripnick(origin)), s(ctx->dbcon, origin), s(ctx->dbcon, params[0]), s(ctx->dbcon, params[1]));
	if (mysql_query(ctx->dbcon, stmt))
	{
		fprintf(stderr, "%s\n", mysql_error(ctx->dbcon));
		mysql_close(ctx->dbcon);
		exit(1);
	}
	
	free(stmt);
}

void event_channel_notice(irc_session_t* session, const char* event, const char* origin, const char** params, unsigned int count)
{
	// Make sure there's actually a message attached
	if (count < 2)
	{
		return;
	}
	
	// Log message
	irc_ctx_t* ctx = (irc_ctx_t*) irc_get_ctx(session);
	
	char* stmt = (char*) malloc(1280*sizeof(char));
	sprintf(stmt, "INSERT INTO messages (type,who,raw_nick,channel,body) VALUES (\"notice\",\"%s\",\"%s\",\"%s\",\"%s\")", s(ctx->dbcon, stripnick(origin)), s(ctx->dbcon, origin), s(ctx->dbcon, params[0]), s(ctx->dbcon, params[1]));
	if (mysql_query(ctx->dbcon, stmt))
	{
		fprintf(stderr, "%s\n", mysql_error(ctx->dbcon));
		mysql_close(ctx->dbcon);
		exit(1);
	}
	
	free(stmt);
}

int main(int argc, char** args)
{
	irc_ctx_t ctx;
	
	// Load all the required information from the config file
	YAML::Node config = YAML::LoadFile("config.yml");
	const char* irc_hostname = config["irc_hostname"].as<std::string>().c_str();
	int irc_port = config["irc_port"].as<int>();
	const char* irc_nick = config["irc_nick"].as<std::string>().c_str();
	char* db_hostname = _(config["db_hostname"].as<std::string>().c_str());
	int db_port = config["db_port"].as<int>();
	char* db_username = _(config["db_username"].as<std::string>().c_str());
	char* db_password = _(config["db_password"].as<std::string>().c_str());
	char* db_database = _(config["db_database"].as<std::string>().c_str());
	ctx.config = config;
	
	// Initialize the database connection
	MYSQL* con = mysql_init(NULL);
	if (con == NULL)
	{
		fprintf(stderr, "%s\n", mysql_error(con));
		exit(1);
	}
	
	if (mysql_real_connect(con, db_hostname, db_username, db_password, db_database, db_port, NULL, 0) == NULL)
	{
		fprintf(stderr, "%s\n", mysql_error(con));
		mysql_close(con);
		exit(1);
	}
	
	ctx.dbcon = con;
	ctx.nick = new std::string(irc_nick);

	// Set up the IRC callbacks
	irc_callbacks_t callbacks;
	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.event_connect = event_connect;
	callbacks.event_numeric = event_numeric;
	callbacks.event_nick = event_nick;
	callbacks.event_quit = event_quit;
	callbacks.event_join = event_join;
	callbacks.event_part = event_part;
	callbacks.event_mode = event_mode;
	callbacks.event_kick = event_kick;
	callbacks.event_topic = event_topic;
	callbacks.event_channel = event_channel;
	callbacks.event_ctcp_action = event_ctcp_action;
	callbacks.event_channel_notice = event_channel_notice;
	
	// Create the session
	irc_session_t* session = irc_create_session(&callbacks);
	if (!session)
	{
		// Handle the error
		printf("Error creating session\n");
		exit(1);
	}
	
	// We need to carry around our database connection and config
	irc_set_ctx(session, &ctx);
	
	if (irc_connect(session, irc_hostname, irc_port, 0, irc_nick, "topiclogger", "TopicLogger"))
	{
		printf("Could not connect: %s\n", irc_strerror(irc_errno(session)));
		exit(1);
	}
	
	if (irc_run(session))
	{
		// Handle the error
		printf("Could not connect or I/O error: %s\n", irc_strerror(irc_errno(session)));
		exit(1);
	}
}