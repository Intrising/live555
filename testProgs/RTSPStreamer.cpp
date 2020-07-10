#include <string>

#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>

UsageEnvironment* env;
H265VideoStreamFramer* videoSource;
ADTSAudioFileSource* audioSource;

RTPSink* videoSink;
RTPSink* audioSink;

void play();

int main(int argc, char** argv) {

  if(argc < 10) {
    printf("Usage: RTMPStreamer <rtsp_port> <multicast_enable> <multicast_target> <multicast_port> <http_enable> <http_port> <auth_enable> <username> <password>\n");
    exit(1);
  }

  int rtsp_port = atoi(argv[1]);
  bool multicast_enable = atoi(argv[2]);
  std::string multicast_target = argv[3];
  int multicast_port = atoi(argv[4]);
  bool over_http_enable = atoi(argv[5]);
  int over_http_port = atoi(argv[6]);
  bool auth_enable = atoi(argv[7]);
  std::string username = argv[8];
  std::string password = argv[9];

  printf("=======RTSP setting======\n");
  printf("rtsp port = %d\n", rtsp_port);
  printf("multicast enable %d\n", multicast_enable);
  printf("multicast target %s\n", multicast_target.c_str());
  printf("multicast port %d\n", multicast_port);
  printf("http enable %d\n", over_http_enable);
  printf("http port %d\n", over_http_port);
  printf("auth enable %d\n", auth_enable);
  printf("username %s\n", username.c_str());
  printf("password %s\n", password.c_str());

  // Begin by setting up our usage environment:
  TaskScheduler* scheduler = BasicTaskScheduler::createNew();
  env = BasicUsageEnvironment::createNew(*scheduler);


  ServerMediaSession* sms
    = ServerMediaSession::createNew(*env, "live", NULL,
		   "Session streamed by \"VideoStreamer\"",
					   True /*SSM*/);

  // Create 'groupsocks' for RTP and RTCP:
  struct in_addr destinationAddress;
  destinationAddress.s_addr = our_inet_addr(multicast_target.c_str());

  const unsigned short rtpPortNum = multicast_port;
  const unsigned short rtcpPortNum = rtpPortNum+1;
  const unsigned char ttl = 255;

  const Port rtpPort(rtpPortNum);
  const Port rtcpPort(rtcpPortNum);

  // Create a 'H265 Video RTP' sink from the RTP 'groupsock':
  OutPacketBuffer::maxSize = 5000000;
  // Create (and start) a 'RTCP instance' for this RTP sink:
  const unsigned estimatedSessionBandwidth = 1000000; // in kbps; for RTCP b/w share
  const unsigned maxCNAMElen = 100;
  unsigned char CNAME[maxCNAMElen+1];
  gethostname((char*)CNAME, maxCNAMElen);
  CNAME[maxCNAMElen] = '\0'; // just in case

  Groupsock rtpGroupsock(*env, destinationAddress, rtpPort, ttl);
  rtpGroupsock.multicastSendOnly(); // we're a SSM source
  Groupsock rtcpGroupsock(*env, destinationAddress, rtcpPort, ttl);
  rtcpGroupsock.multicastSendOnly(); // we're a SSM source

  Groupsock a_rtpGroupsock(*env, destinationAddress, rtpPort, ttl);
  a_rtpGroupsock.multicastSendOnly(); // we're a SSM source
  Groupsock a_rtcpGroupsock(*env, destinationAddress, rtcpPort, ttl);
  a_rtcpGroupsock.multicastSendOnly(); // we're a SSM source



  if(multicast_enable) {
    RTCPInstance* rtcp
    = RTCPInstance::createNew(*env, &rtcpGroupsock,
			    estimatedSessionBandwidth, CNAME,
			    videoSink, NULL /* we're a server */,
			    True /* we're a SSM source */);

    RTCPInstance* audio_rtcp
    = RTCPInstance::createNew(*env, &a_rtcpGroupsock,
			    estimatedSessionBandwidth, CNAME,
			    audioSink, NULL /* we're a server */,
			    True /* we're a SSM source */);

    // Open the input file as a 'byte-stream file source':
    ByteStreamFileSource* fileSource
      = ByteStreamFileSource::createNew(*env, "/tmp/video.sock");
    if (fileSource == NULL) {
      *env << "Unable to open file \"socket"
           << "\" as a byte-stream file source\n";
      exit(1);
    }

    FramedSource* videoES = fileSource;
    // Create a framer for the Video Elementary Stream:
    videoSource = H265VideoStreamFramer::createNew(*env, videoES);
    videoSink = H265VideoRTPSink::createNew(*env, &rtpGroupsock, 96);

    // Receive frame from unix sock
    audioSource = ADTSAudioFileSource::createNew(*env, "/tmp/audio.sock");
    // 97 is from ADTSAudioFileServerMediaSubsession
    audioSink = MPEG4GenericRTPSink::createNew(*env, &a_rtpGroupsock,
                                             97,
                                             audioSource->samplingFrequency(),
                                             "audio", "AAC-hbr", audioSource->configStr(),
                                             audioSource->numChannels());

    sms->addSubsession(PassiveServerMediaSubsession::createNew(*videoSink, rtcp));
    sms->addSubsession(PassiveServerMediaSubsession::createNew(*audioSink, audio_rtcp));
  }
  else {
    sms->addSubsession(H265VideoFileServerMediaSubsession
                       ::createNew(*env, "/tmp/video.sock", true));
    sms->addSubsession(ADTSAudioFileServerMediaSubsession
		       ::createNew(*env, "/tmp/audio.sock", true));
  }



  // Note: This starts RTCP running automatically
  RTSPServer* rtspServer;
  if(auth_enable) {
    UserAuthenticationDatabase* authDB = NULL;
    authDB = new UserAuthenticationDatabase;
    authDB->addUserRecord(username.c_str(), password.c_str());
    rtspServer = RTSPServer::createNew(*env, rtsp_port, authDB);
  }
  else {
    rtspServer = RTSPServer::createNew(*env, rtsp_port);
  }

  if (rtspServer == NULL) {
    *env << "Failed to create RTSP server: " << env->getResultMsg() << "\n";
    exit(1);
  }



  rtspServer->addServerMediaSession(sms);

  char* url = rtspServer->rtspURL(sms);
  *env << "Play this stream using the URL \"" << url << "\"\n";
  delete[] url;

  if(over_http_enable) {
    if(rtspServer->setUpTunnelingOverHTTP(over_http_port)) {
      *env << "Enable RTSP over HTTP tunneling with port " << rtspServer->httpServerPortNum() << "\n";
    }
  }

  if(multicast_enable)
    play();

  env->taskScheduler().doEventLoop(); // does not return

  return 0;
}

void afterPlaying(void* /*clientData*/) {
  *env << "...done streaming from encoder\n";
  videoSink->stopPlaying();
  Medium::close(videoSource);
  audioSink->stopPlaying();
  Medium::close(audioSource);
  play();
}

void play() {

  *env << "Beginning to streaming from encoder...\n";
  audioSink->startPlaying(*audioSource, NULL, audioSink);  
  videoSink->startPlaying(*videoSource, NULL, videoSink);

}
