/**
 * main.cpp - TopicLogger
 */

#include <yaml-cpp/yaml.h>
#include <libircclient.h>
#include <libirc_rfcnumeric.h>
#include <vector>
#include <my_global.h>
#include <mysql.h>

typedef struct
{
	MYSQL* dbcon;
	YAML::Node config;
} irc_ctx_t;

char* _(const char* str)
{
	char* res = new char[strlen(str)+1];
	strcpy(res, str);
	return res;
}

void event_connect(irc_session_t* session, const char* event, const char* origin, const char** params, unsigned int count)
{
	printf("Connected to IRC\n");
	
	irc_ctx_t* ctx = (irc_ctx_t*) irc_get_ctx(session);

	// Identify with NickServ
	std::string password = ctx->config["irc_password"].as<std::string>();
	irc_cmd_msg(session, "NickServ", (std::string("IDENTIFY ") + password).c_str());
	
	// Join channels
	std::vector<std::string> channels = ctx->config["irc_channels"].as<std::vector<std::string> >();
	for (std::vector<std::string>::iterator it = channels.begin(); it != channels.end(); ++it)
	{
		irc_cmd_join(session, it->c_str(), 0);
	}
}

void event_numeric(irc_session_t* session, unsigned int event, const char* origin, const char** params, unsigned int count)
{
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
	}
}

void event_nick(irc_session_t* session, const char* event, const char* origin, const char** params, unsigned int count)
{
	
}

void event_quit(irc_session_t* session, const char* event, const char* origin, const char** params, unsigned int count)
{
	
}

void event_join(irc_session_t* session, const char* event, const char* origin, const char** params, unsigned int count)
{
	
}

void event_part(irc_session_t* session, const char* event, const char* origin, const char** params, unsigned int count)
{
	
}

void event_mode(irc_session_t* session, const char* event, const char* origin, const char** params, unsigned int count)
{
	
}

void event_kick(irc_session_t* session, const char* event, const char* origin, const char** params, unsigned int count)
{
	
}

void event_channel(irc_session_t* session, const char* event, const char* origin, const char** params, unsigned int count)
{
	
}

void event_ctcp_action(irc_session_t* session, const char* event, const char* origin, const char** params, unsigned int count)
{
	
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
	callbacks.event_channel = event_channel;
	callbacks.event_ctcp_action = event_ctcp_action;
	
	// Create the session
	irc_session_t* session = irc_create_session(&callbacks);
	if (!session)
	{
		// Handle the error
		printf("Error creating session\n");
		exit(1);
	}
	
	// We don't want hostmasks in our nicks
	irc_option_set(session, LIBIRC_OPTION_STRIPNICKS);
	
	// We need to carry around our database connection and config
	irc_set_ctx(session, &ctx);
	
	if (irc_connect(session, irc_hostname, irc_port, 0, irc_nick, 0, "TopicLogger"))
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