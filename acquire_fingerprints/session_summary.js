function get_session_summary() {
    const videoPlayer = netflix.appContext.getPlayerApp().getAPI().videoPlayer;
    // Getting player id
    const playerSessionId = videoPlayer.getAllPlayerSessionIds()[0];
    const player = videoPlayer.getVideoPlayerBySessionId(playerSessionId);
    
    var summary = player.getSessionSummary()
    var avg_vbr = summary.init_vbr
    while(summary.bufferedTime <= 300000) {

        summary = player.getSessionSummary();
        avg_vbr = (summary.init_vbr + avg_vbr) / 2;
        
    }

    return avg_vbr;

} 

return get_session_summary();
