//
// Created by salimterryli on 2021/3/10.
//

#ifndef MCWRAPPER_SERVERINFO_H
#define MCWRAPPER_SERVERINFO_H

#include <time.h>

#include <list>

using namespace std;

struct Server;
struct Player;

struct Server {
	char version[16] = "";
	char resource_name[64] = "";
	char default_mode[32] = "";
	char level_name[64] = "";
	char bind_addr[32] = "";
	float bootup_time = 0;
	char rcon_bind_addr[32] = "";
	time_t boot_ts = {};

	list<Player> players;
};

struct Player {
	char name[64] = "";
	char uuid[37] = "";
	bool isOnline = false;
	float login_pos[3] = {};
	time_t login_ts = {};

	float *getSavedPos();
};

extern struct Server serverHolder;

#endif// MCWRAPPER_SERVERINFO_H
