function rewind() {
    const videoPlayer = netflix.appContext.getPlayerApp().getAPI().videoPlayer;
     //Getting player id
    const playerSessionId = videoPlayer.getAllPlayerSessionIds()[0];
    const player = videoPlayer.getVideoPlayerBySessionId(playerSessionId);

    player.seek(-9999999999999);
}

return rewind();

