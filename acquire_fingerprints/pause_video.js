function pause_video() {
    const videoPlayer = netflix.appContext.getPlayerApp().getAPI().videoPlayer;
     //Getting player id
    const playerSessionId = videoPlayer.getAllPlayerSessionIds()[0];
    const player = videoPlayer.getVideoPlayerBySessionId(playerSessionId);

    player.pause();
    return false
}

return pause_video();
