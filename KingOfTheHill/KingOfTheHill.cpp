#include "pch.h"
#include "KingOfTheHill.h"


BAKKESMOD_PLUGIN(KingOfTheHill, "King of the Hill game mode", plugin_version, 0)

std::shared_ptr<CVarManagerWrapper> _globalCvarManager;

std::list<int> gamersQueue;
std::list<int> gamersPlaying;

void KingOfTheHill::onLoad()
{
    _globalCvarManager = cvarManager;
    cvarManager->log("KingOfTheHill loaded!");
    gameWrapper->HookEventWithCallerPost<ServerWrapper>("Function TAGame.GFxHUD_TA.HandleStatTickerMessage",
        [this](ServerWrapper caller, void* params, const std::string&) {
            if (!isGameServer()) { return; }
            statEvent(params);
        });
    gameWrapper->HookEvent("Function TAGame.GameEvent_TA.EventPlayerAdded",
        [this](const std::string& name) {
            if (!isGameServer()) { return; }
            cvarManager->log("EventPlayerAdded");
            updateGamersQueue();
            updateGamersPlaying();
        });
    gameWrapper->HookEvent("Function TAGame.GameEvent_Soccar_TA.EventMatchEnded",
        [this](const std::string& name) {
            gamersQueue.clear();
            gamersPlaying.clear();
            _globalCvarManager->log("EVENT: " + name);
        });
}

void KingOfTheHill::onUnload()
{
    cvarManager->log("KingOfTheHill unloading...");
    gameWrapper->UnhookEventPost("Function TAGame.GFxHUD_TA.HandleStatTickerMessage");
    gameWrapper->UnhookEvent("Function TAGame.GameEvent_TA.EventPlayerAdded");
    gameWrapper->UnhookEvent("Function TAGame.GameEvent_Soccar_TA.EventMatchEnded");
}
bool KingOfTheHill::isGameServer() {
    bool ret = true;
    std::string gameMode = "";
    if (gameWrapper->IsInFreeplay()) {
        gameMode = "freeplay";
        ret = false;
    }
    else if (gameWrapper->IsInReplay()) {
        gameMode = "replay";
        ret = false;
    }
    else if (gameWrapper->IsInCustomTraining()) {
        gameMode = "custom training";
        ret = false;
    }
    else if (gameWrapper->IsInOnlineGame()) {
        gameMode = "online game";
        ret = false;
    }
    else if (gameWrapper->IsInGame()) {
        gameMode = "game";
    }
    cvarManager->log("KingOfTheHill in " + gameMode);
    return ret;
}



const ServerWrapper& KingOfTheHill::getGameServer() {
    //cvarManager->log("getting game server");
    if (gameWrapper->IsInGame()) {
        return gameWrapper->GetGameEventAsServer();
    }
    else if (gameWrapper->IsInOnlineGame()) {
        return gameWrapper->GetOnlineGame();
    }
    return gameWrapper->GetGameEventAsServer();
}

void KingOfTheHill::updateGamersQueue() {
    //ServerWrapper sw = gameWrapper->GetGameEventAsServer();
    ServerWrapper sw = getGameServer();
    if (sw.IsNull()) {
        cvarManager->log("[updateGamersQueue] null game server");
        return;
    }
    auto cars = sw.GetCars();
    auto players = sw.GetPlayers();
    auto localPlayers = sw.GetLocalPlayers();
    auto PRIs = sw.GetPRIs();

    _globalCvarManager->log("Updating queue. Cars: " + std::to_string(cars.Count()) + ". players: " + std::to_string(players.Count()) + ". Local: " + std::to_string(localPlayers.Count()) + ". PRIs: " + std::to_string(PRIs.Count()));
    for (auto p : PRIs) {
        if (p.IsNull() || p.GetbBot()) {
            continue;
        }
        auto pid = p.GetPlayerID();
        _globalCvarManager->log("Found player by PRI: " + std::to_string(pid));
        if (std::find(gamersQueue.begin(), gamersQueue.end(), pid) == gamersQueue.end()
            && std::find(gamersPlaying.begin(), gamersPlaying.end(), pid) == gamersPlaying.end()) {
            gamersQueue.push_back(pid);
            _globalCvarManager->log("New Queue PID: " + std::to_string(pid));
        }
    }
}

void KingOfTheHill::updateGamersPlaying() {
    ServerWrapper sw = getGameServer();
    if (sw.IsNull()) {
        cvarManager->log("[updateGamersPlaying] null game server");
        return;
    }
    auto players = sw.GetPlayers();
    auto teams = sw.GetTeams();
    while (players.Count() > 1 && gamersPlaying.size() < 2) {
        for (auto team : teams) {
            auto humans = team.GetMembers().Count() - team.GetNumBots();
            if (humans <= 0) {
                faceNextPlayer(team.GetTeamNum());
                break;
            }
        }
    }
}

struct StatEventStruct
{
    uintptr_t Receiver;
    uintptr_t Victim;
    uintptr_t StatEvent;
};

void KingOfTheHill::statEvent(void* args) {
    auto stats = (StatEventStruct*)args;

    ServerWrapper sw = getGameServer();

    //auto victim = PriWrapper(tArgs->Victim);
    auto scorer = PriWrapper(stats->Receiver);
    auto statEvent = StatEventWrapper(stats->StatEvent);
    if (statEvent.IsNull() || scorer.IsNull() || sw.IsNull()) {
        return;
    }

    std::string eventString = statEvent.GetEventName();
    _globalCvarManager->log("event type: " + eventString + " by " + scorer.GetPlayerName().ToString());
    if (eventString != "Goal") {
        return;
    }
    _globalCvarManager->log(scorer.GetPlayerName().ToString() + " SCORED!");
    auto scorerTeam = scorer.GetTeamNum();
    auto victimsTeam = (scorerTeam + 1) % 2;
    switch (gameMode) {
    case GameMode::LOOSER:
    {
        for (CarWrapper car : sw.GetCars()) {
            if (car.IsNull()) {
                continue;
            }
            if (car.GetTeamNum2() != scorerTeam) {
                continue;
            }
            auto pri = car.GetPRI();
            if (pri.IsNull()) {
                continue;
            }
            if (!pri.GetbBot() && gamersPlaying.size() >= 2) {
                kickAndSwap(pri, scorerTeam);
                break;
            }
        }
    }
    break;
    case GameMode::WINNER:
    {
        auto teams = sw.GetTeams();
        for (auto team : teams) {
            if (team.GetTeamNum() == scorerTeam) {
                auto goals = team.GetScore();
                if (goals >= kothWins) {
                    sw.SetGameTimeRemaining(.0f);
                    //sw.SetGameWinner(team);
                    //sw.SetMVP(team.GetMembers().Get(0));
                    return;
                }
            }
            else if (team.GetTeamNum() == victimsTeam) {
                team.ResetScore();
            }
        }
        for (auto p : sw.GetPRIs()) {
            if (p.IsNull()) {
                continue;
            }
            if (p.GetTeamNum2() != victimsTeam) {
                continue;
            }
            _globalCvarManager->log("player scored on: " + p.GetPlayerName().ToString() + " " + (p.GetbBot() ? "(bot) " : "") + "in the game with " + std::to_string(gamersPlaying.size()) + " players");
            if (!p.GetbBot() && gamersPlaying.size() >= 2) {
                kickAndSwap(p, victimsTeam);
                break;
            }
        }
    }
    break;
    }
}

void KingOfTheHill::kickAndSwap(PriWrapper playerToKick, int team) {
    // Now we have the player that got scored on. We have to reset all stats and send them to spectate.
    playerToKick.SetMatchGoals(0);
    playerToKick.SetMatchShots(0);
    playerToKick.SetMatchSaves(0);
    playerToKick.SetMatchScore(0);
    playerToKick.ServerSpectate();
    auto pid = playerToKick.GetPlayerID();
    gamersQueue.push_back(pid);
    gamersPlaying.remove(pid);
    _globalCvarManager->log("Removed playing and added gamersQueue PID: " + std::to_string(playerToKick.GetPlayerID()));
    faceNextPlayer(team);
}

void KingOfTheHill::faceNextPlayer(int team) {
    ServerWrapper sw = getGameServer();
    auto PRIs = sw.GetPRIs();
    bool changed = false;
    int nextPlayer;
    _globalCvarManager->log("[faceNextPlayer]   ");
    while (!changed && gamersQueue.size() > 0) {
        nextPlayer = gamersQueue.front();
        for (auto p : PRIs) {
            if (p.IsNull() || p.GetbBot()) continue;
            if (p.GetPlayerID() != nextPlayer) continue;
            _globalCvarManager->log("Moving PID: " + std::to_string(p.GetPlayerID()) + " to team: " + std::to_string(team));
            p.ServerChangeTeam(team);
            changed = true;
        }
        gamersQueue.pop_front();
    }
    if (changed) {
        gamersPlaying.push_back(nextPlayer);
        _globalCvarManager->log("New gamersPlaying PID: " + std::to_string(nextPlayer));
    }
}