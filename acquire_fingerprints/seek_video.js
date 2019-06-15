function seek_video() {
    const videoPlayer = netflix.appContext.getPlayerApp().getAPI().videoPlayer;
     //Getting player id
    const playerSessionId = videoPlayer.getAllPlayerSessionIds()[0];
    const player = videoPlayer.getVideoPlayerBySessionId(playerSessionId);

    player.seek(player.getCurrentTime() + 720000);
    return player.getCurrentTime();
}

return seek_video();
