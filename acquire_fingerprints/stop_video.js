function stop_playback() {
    const videoPlayer = netflix.appContext.getPlayerApp().getAPI().videoPlayer;
    // Getting player id
    const playerSessionId = videoPlayer.getAllPlayerSessionIds()[0];
    const player = videoPlayer.getVideoPlayerBySessionId(playerSessionId);
    
    return player.getBufferedTime()
} 

return stop_playback();
