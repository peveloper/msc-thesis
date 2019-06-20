function get_player(){const e=netflix.appContext.state.playerApp.getAPI().videoPlayer,t=e.getAllPlayerSessionIds()[0];return e.getVideoPlayerBySessionId(t)} return get_player()

