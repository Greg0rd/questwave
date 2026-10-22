package com.questwave.player.plugin;

import com.questwave.player.GameDataParcel;

interface AsyncCallback {
    void onSuccess(inout List<GameDataParcel> gameDataParcel);
}