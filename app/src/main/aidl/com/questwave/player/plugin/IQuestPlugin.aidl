package com.questwave.player.plugin;

import com.questwave.player.plugin.AsyncCallback;

interface IQuestPlugin {
    // API
    String versionPlugin();
    String titlePlugin();
    String authorPlugin();

    // Plugin part
    void arrayGameData(AsyncCallback callback);
}