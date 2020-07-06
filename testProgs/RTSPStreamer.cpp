#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"

UsageEnvironment* env;
RTPSink *video_sink = NULL;
H265VideoStreamFramer* videoSource;

void play();

void afterPlaying(void* /*clientData*/) {
  *env << "...done reading from file\n";
  video_sink->stopPlaying();
  Medium::close(videoSource);
  // Note that this also closes the input file that this source read from.

  // Start playing once again:
  play();
}

void play() {
  // Open the input file as a 'byte-stream file source':
  ByteStreamFileSource* fileSource
    = ByteStreamFileSource::createNew(*env, "/tmp/video.sock");
  if (fileSource == NULL) {
    *env << "Unable to open socket"
         << "\" as a byte-stream file source\n";
    exit(1);
  }

  FramedSource* videoES = fileSource;

  // Create a framer for the Video Elementary Stream:
  videoSource = H265VideoStreamFramer::createNew(*env, videoES);

  // Finally, start playing:
  *env << "Beginning to read from file...\n";
  video_sink->startPlaying(*videoSource, afterPlaying, video_sink);
}



int main(int argc, char** argv) {
  // Begin by setting up our usage environment:
  TaskScheduler* scheduler = BasicTaskScheduler::createNew();
  env = BasicUsageEnvironment::createNew(*scheduler);

  UserAuthenticationDatabase* authDB = NULL;
#ifdef ACCESS_CONTROL
  // To implement client access control to the RTSP server, do the following:
  authDB = new UserAuthenticationDatabase;
  authDB->addUserRecord("username1", "password1"); // replace these with real strings
  // Repeat the above with each <username>, <password> that you wish to allow
  // access to the server.
#endif


  int rtsp_port = 5554;
  int over_http_port = 8000;

  // Create the RTSP server:
  RTSPServer* rtspServer = RTSPServer::createNew(*env, rtsp_port, authDB);
  if (rtspServer == NULL) {
    *env << "Failed to create RTSP server: " << env->getResultMsg() << "\n";
    exit(1);
  }

  char const* descriptionString = "NVS Streaming server";
  char const* streamName = "live";
  char const *input_audio_sockname = "/tmp/audio.sock";
  char const *input_video_sockname = "/tmp/video.sock";

  ServerMediaSession* sms = ServerMediaSession::createNew(*env,
      streamName, streamName, descriptionString);
  H265VideoFileServerMediaSubsession *video_framed_source = H265VideoFileServerMediaSubsession::createNew(*env,
      input_video_sockname, false);
  sms->addSubsession(video_framed_source);

  sms->addSubsession(ADTSAudioFileServerMediaSubsession::createNew(*env,
      input_audio_sockname, false));

  rtspServer->addServerMediaSession(sms);

  *env << "Stream URL: " << rtspServer->rtspURL(sms) << "\n";

  // Enable RTSP over HTTP
  if(rtspServer->setUpTunnelingOverHTTP(over_http_port)) {
    *env << "Enable RTSP over HTTP tunneling with port " << rtspServer->httpServerPortNum() << "\n";
  }

  env->taskScheduler().doEventLoop(); // does not return

  return 0;
}



