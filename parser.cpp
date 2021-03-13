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
        R"(^Done \(([0-9]+.[0-9]+)s\)! For help, type "help")");// grp1: time
                                                                // float sec

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
	std::cmatch pieces_match;
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
			}
		} else if (state.level == WARN) {
			//
		}
	} else if (strstr(state.src_id, "main") == state.src_id) {
		if (state.level == INFO) {
			if (std::regex_search(line, pieces_match, regex_l3_resourcemanager)) {
				strcpy(serverHolder.resource_name, pieces_match[1].str().c_str());
			}
		} else if (state.level == WARN) {
			//
		}
	} else if (strstr(state.src_id, "User Authenticator") == state.src_id) {
		if (state.level == INFO) {
			//
		} else if (state.level == WARN) {
			//
		}
	}
	return false;
}

void line_parse_stdout(const char *line) { first_stage(line); }

void line_parse_stderr(const char *line) {
	printf("subprocess stderr: %s\n", line);
}