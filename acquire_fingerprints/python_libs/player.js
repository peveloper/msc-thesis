
function get_player() {
    const videoPlayer = netflix.appContext.state.playerApp.getAPI().videoPlayer;
    // Getting player id
    const playerSessionId = videoPlayer.getAllPlayerSessionIds()[0];
    const player = videoPlayer.getVideoPlayerBySessionId(playerSessionId);
    return player;
} 

return get_player();
