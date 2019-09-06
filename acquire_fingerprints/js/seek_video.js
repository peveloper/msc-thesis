function seek_video() {
    const videoPlayer = netflix.appContext.getPlayerApp().getAPI().videoPlayer;
     //Getting player id
    const playerSessionId = videoPlayer.getAllPlayerSessionIds()[0];
    const player = videoPlayer.getVideoPlayerBySessionId(playerSessionId);

    window.orig = XMLHttpRequest.prototype.send;

    XMLHttpRequest.prototype.send = function() {
        return false;
    }

    player.seek(-999999999);
    player.seek(240000);

    return true;
}

return seek_video();
