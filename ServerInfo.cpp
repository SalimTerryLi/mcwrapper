//
// Created by salimterryli on 2021/3/11.
//

#include "ServerInfo.h"
#include <cstring>

struct Server serverHolder;
pthread_mutex_t serverHolder_mutex = {};

float *Player::getSavedPos() {
	return nullptr;
}

int count_online_player() {
	int ret = 0;
	pthread_mutex_lock(&serverHolder_mutex);
	for (Player player : serverHolder.players) {
		if (player.isOnline) {
			++ret;
		}
	}
	pthread_mutex_unlock(&serverHolder_mutex);
	return ret;
}

bool Player::operator==(const Player &rhs) const {
	return strstr(name, rhs.name) == name;
}
bool Player::operator!=(const Player &rhs) const {
	return !(rhs == *this);
}