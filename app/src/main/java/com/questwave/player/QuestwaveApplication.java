package com.questwave.player;

import android.app.Application;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.content.Context;
import android.os.Build;

import androidx.documentfile.provider.DocumentFile;

import com.questwave.player.model.libQP.LibQpProxy;
import com.questwave.player.model.libQP.LibQpProxyImpl;
import com.questwave.player.model.service.AudioPlayer;
import com.questwave.player.model.service.GameContentResolver;
import com.questwave.player.model.service.HtmlProcessor;
import com.questwave.player.model.service.ImageProvider;

public class QuestwaveApplication extends Application {

    public static final int INSTALL_GAME_NOTIFICATION_ID = 1800;

    public static final int POST_INSTALL_GAME_NOTIFICATION_ID = 1801;

    public static final String CHANNEL_INSTALL_GAME = "com.questwave.player.channel.install_game";

    private final GameContentResolver gameContentResolver = new GameContentResolver();
    private final ImageProvider imageProvider = new ImageProvider();
    private final HtmlProcessor htmlProcessor = new HtmlProcessor(gameContentResolver, imageProvider);
    private final AudioPlayer audioPlayer = new AudioPlayer();
    private final LibQpProxyImpl libQspProxy = new LibQpProxyImpl(this, gameContentResolver, htmlProcessor, audioPlayer);

    private DocumentFile customRootDir;

    @Override
    public void onCreate() {
        super.onCreate();
        createNotificationChannels();
    }

    public void setCustomRootFolder(DocumentFile customRootDir) {
        this.customRootDir = customRootDir;
    }

    public GameContentResolver getGameContentResolver() {
        return gameContentResolver;
    }

    public HtmlProcessor getHtmlProcessor() {
        return htmlProcessor;
    }

    public AudioPlayer getAudioPlayer() {
        return audioPlayer;
    }

    public LibQpProxy getLibQspProxy() {
        return libQspProxy;
    }

    public DocumentFile getCustomRootDir() {
        return customRootDir;
    }

    public void createNotificationChannels() {
        var notificationManager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        var importance = NotificationManager.IMPORTANCE_LOW;

        var name = getString(R.string.channelInstallGame);
        var channel =  new NotificationChannel(CHANNEL_INSTALL_GAME, name, importance);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            channel.setAllowBubbles(true);
        }
        notificationManager.createNotificationChannel(channel);

    }
}