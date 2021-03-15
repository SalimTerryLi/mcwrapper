//
// Created by salimterryli on 2021/3/9.
//

#include "parser.h"

#include <cstdio>
#include <regex>

#include "ServerInfo.h"
#include "utils.h"

enum OutputLevel { UNKNOWN = 0,
	               INFO,
	               WARN,
	               ERROR };

struct ParserState {
	time_t timestamp = {};
	char src_id[32] = "";
	OutputLevel level = UNKNOWN;
};

const std::regex regex_l1(R"(^\[(?:(?:[0-1][0-9])|(2[0-3]))(?::[0-5][0-9]){2}\] )");
const std::regex regex_l2(R"(^\[([^\]]+)\/(\w+)]: )");

const std::regex regex_l3_playermsg(
        R"(^<([^>]+)> ([\s\S]*))");// grp1: playername, grp2: msg
const std::regex regex_l3_resourcemanager(
        R"(^Reloading ResourceManager: ([\s\S]*))");                                             // grp1: name
const std::regex regex_l3_starting_server_ver(R"(^Starting minecraft server version ([\s\S]+))");// grp1: version
const std::regex regex_l3_starting_server_addr(
        R"(^Starting Minecraft server on ([\s\S]+:[0-9]+))");// grp1: addr
const std::regex regex_l3_rcon_addr(
        R"(^RCON running on ([\S]+:[0-9]+))");// grp1: addr
const std::regex regex_l3_default_mode(
        R"(^Default game type: ([\w]+))");// grp1: mode
const std::regex regex_l3_preparing_level(
        R"~(^Preparing level "([\s\S]+)")~");// grp1: level name
const std::regex regex_l3_booting_time(
        R"(^Done \(([0-9]+.[0-9]+)s\)! For help, type "help")");                     // grp1: time
                                                                                     // float sec
const std::regex regex_l3_player_uuid(R"(^UUID of player ([\S]+) is ([0-9a-f\-]+))");// grp1: name, grp2: uuid
const std::regex regex_l3_player_login(
        R"(^([\S]+)\[\/([\S.:]+)\] logged in with entity id [0-9]+ at \(([-]?[0-9]+.[0-9]+), ([-]?[0-9]+.[0-9]+), ([-]?[0-9]+.[0-9]+)\))");// grp1: name, grp2: path, grp3~5: x,y,z
const std::regex regex_l3_player_logined(R"(^([\S]+) joined the game)");
const std::regex regex_l3_player_left_game(R"(^([\S]+) left the game)");

/*
 * parse timestamp
 * must match [xx:xx:xx]
 * then system timestamp is stored
 */
bool first_stage(const char *line);

/*
 * parse message source and level
 */
bool second_stage(ParserState &state, const char *line);

/*
 * actual payload parser
 */
bool third_stage(ParserState &state, const char *line);

bool first_stage(const char *line) {
	struct ParserState state = {};
	std::cmatch pieces_match;
	if (std::regex_search(line, pieces_match, regex_l1)) {// matched
		state.timestamp = time(nullptr);
		return second_stage(
		        state,
		        line +
		                pieces_match[0]
		                        .length());// skip beginning, this index should not overflow
	} else {
		return false;
	}
}

bool second_stage(ParserState &state, const char *line) {
	std::cmatch pieces_match;
	if (std::regex_search(line, pieces_match, regex_l2)) {
		strcpy(state.src_id, pieces_match[1].str().c_str());
		if (pieces_match[2].str() == "INFO") {
			state.level = INFO;
		} else if (pieces_match[2].str() == "WARN") {
			state.level = WARN;
		} else if (pieces_match[2].str() == "ERROR") {
			state.level = ERROR;// TODO
		}

		return third_stage(state, line + pieces_match[0].length());
		;
	} else {
		return false;
	}
}

bool third_stage(ParserState &state, const char *line) {
	bool ret = true;
	std::cmatch pieces_match;
	pthread_mutex_lock(&serverHolder_mutex);
	if (strstr(state.src_id, "Server thread") == state.src_id) {
		if (state.level == INFO) {
			if (std::regex_search(line, pieces_match,
			                      regex_l3_playermsg)) {// player message
				printf("player %s : %s\n", pieces_match[1].str().c_str(),
				       pieces_match[2].str().c_str());
			} else if (std::regex_search(
			                   line, pieces_match,
			                   regex_l3_starting_server_ver)) {// server version
				strcpy(serverHolder.version, pieces_match[1].str().c_str());
			} else if (std::regex_search(
			                   line, pieces_match,
			                   regex_l3_starting_server_addr)) {// server addr
				strcpy(serverHolder.bind_addr, pieces_match[1].str().c_str());
			} else if (std::regex_search(line, pieces_match,
			                             regex_l3_default_mode)) {// server mode
				strcpy(serverHolder.default_mode, pieces_match[1].str().c_str());
			} else if (std::regex_search(line, pieces_match,
			                             regex_l3_preparing_level)) {// level name
				strcpy(serverHolder.level_name, pieces_match[1].str().c_str());
			} else if (std::regex_search(
			                   line, pieces_match,
			                   regex_l3_booting_time)) {// boot elapsed time
				serverHolder.bootup_time = atof(pieces_match[1].str().c_str());
				serverHolder.boot_ts = time(nullptr);
			} else if (std::regex_search(line, pieces_match,
			                             regex_l3_rcon_addr)) {// rcon addr
				strcpy(serverHolder.level_name, pieces_match[1].str().c_str());
			} else if (std::regex_search(line, pieces_match,
			                             regex_l3_player_login)) {// player login
				bool is_found = false;
				for (Player &player : serverHolder.players) {
					if (strstr(player.name, pieces_match[1].str().c_str())) {
						is_found = true;
						player.login_pos[0] = atof(pieces_match[3].str().c_str());
						player.login_pos[1] = atof(pieces_match[4].str().c_str());
						player.login_pos[2] = atof(pieces_match[5].str().c_str());
						break;
					}
				}
				if (!is_found) {
					Player player = {};
					strcpy(player.name, pieces_match[1].str().c_str());
					player.login_pos[0] = atof(pieces_match[3].str().c_str());
					player.login_pos[1] = atof(pieces_match[4].str().c_str());
					player.login_pos[2] = atof(pieces_match[5].str().c_str());
					serverHolder.players.push_back(player);
				}
			} else if (std::regex_search(line, pieces_match,
			                             regex_l3_player_logined)) {// player login
				bool is_found = false;
				for (Player &player : serverHolder.players) {
					if (strstr(player.name, pieces_match[1].str().c_str())) {
						is_found = true;
						player.login_ts = time(nullptr);
						player.isOnline = true;
						break;
					}
				}
				if (!is_found) {
					eprintf("Invalid player login event! %s", pieces_match[1].str().c_str());
				}
			} else if (std::regex_search(line, pieces_match,
			                             regex_l3_player_left_game)) {// player login
				bool is_found = false;
				for (Player &player : serverHolder.players) {
					if (strstr(player.name, pieces_match[1].str().c_str())) {
						is_found = true;
						serverHolder.players.remove(player);// TODO: not safe, but OK
						break;
					}
				}
				if (!is_found) {
					eprintf("Invalid player left game event! %s", pieces_match[1].str().c_str());
				}
			} else {
				ret = false;
			}
		} else if (state.level == WARN) {
			//
		} else {
			ret = false;
		}
	} else if (strstr(state.src_id, "main") == state.src_id) {
		if (state.level == INFO) {
			if (std::regex_search(line, pieces_match, regex_l3_resourcemanager)) {
				strcpy(serverHolder.resource_name, pieces_match[1].str().c_str());
			}
		} else if (state.level == WARN) {
			//
		} else {
			ret = false;
		}
	} else if (strstr(state.src_id, "User Authenticator") == state.src_id) {
		if (state.level == INFO) {
			if (std::regex_search(line, pieces_match,
			                      regex_l3_player_uuid)) {// player uuid
				bool is_found = false;
				for (Player &player : serverHolder.players) {
					if (strstr(player.name, pieces_match[1].str().c_str())) {
						is_found = true;
						strcpy(player.uuid, pieces_match[2].str().c_str());
						break;
					}
				}
				if (!is_found) {
					Player player = {};
					strcpy(player.name, pieces_match[1].str().c_str());
					strcpy(player.uuid, pieces_match[2].str().c_str());
					serverHolder.players.push_back(player);
				}
			} else {
				ret = false;
			}
		} else if (state.level == WARN) {
			//
		} else {
			ret = false;
		}
	} else {
		ret = false;
	}
	pthread_mutex_unlock(&serverHolder_mutex);
	return ret;
}

void line_parse_stdout(const char *line) { first_stage(line); }

void line_parse_stderr(const char *line) {
	printf("subprocess stderr: %s\n", line);
}