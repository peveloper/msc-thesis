

function get_player_status() {
    const videoPlayer = netflix.appContext.getPlayerApp().getAPI().videoPlayer;
    // Getting player id
    const playerSessionId = videoPlayer.getAllPlayerSessionIds()[0];
    const player = videoPlayer.getVideoPlayerBySessionId(playerSessionId);

    var loaded = player.loaded;
    if (!loaded) { 
        return loaded;
    }


    return loaded;

} 

return get_player_status();
