	/* note: this requires gstreamer 0.10.x and a big list of plugins. */
	/* it's currently hardcoded to use a big-endian alsasink as sink. */
#include <lib/base/ebase.h>
#include <lib/base/eerror.h>
#include <lib/base/init_num.h>
#include <lib/base/init.h>
#include <lib/base/nconfig.h>
#include <lib/base/object.h>
#include <lib/dvb/epgcache.h>
#include <lib/dvb/decoder.h>
#include <lib/components/file_eraser.h>
#include <lib/gui/esubtitle.h>
#include <lib/service/servicemp3.h>
#include <lib/service/servicemp3record.h>
#include <lib/service/service.h>
#include <lib/gdi/gpixmap.h>

#include <string>

#ifdef ENABLE_MEDIAFWGSTREAMER
#include <gst/gst.h>
#include <gst/pbutils/missing-plugins.h>
#endif
#include <sys/stat.h>

#define HTTP_TIMEOUT 60

/*
 * UNUSED variable from service reference is now used as buffer flag for gstreamer
 * REFTYPE:FLAGS:STYPE:SID:TSID:ONID:NS:PARENT_SID:PARENT_TSID:UNUSED
 *   D  D X X X X X X X X
 * 4097:0:1:0:0:0:0:0:0:0:URL:NAME (no buffering)
 * 4097:0:1:0:0:0:0:0:0:1:URL:NAME (buffering enabled)
 * 4097:0:1:0:0:0:0:0:0:3:URL:NAME (progressive download and buffering enabled)
 *
 * Progressive download requires buffering enabled, so it's mandatory to use flag 3 not 2
 */
typedef enum
{
	BUFFERING_ENABLED	= 0x00000001,
	PROGRESSIVE_DOWNLOAD	= 0x00000002
} eServiceMP3Flags;

/*
 * GstPlayFlags flags from playbin2. It is the policy of GStreamer to
 * not publicly expose element-specific enums. That's why this
 * GstPlayFlags enum has been copied here.
 */
typedef enum
{
	GST_PLAY_FLAG_VIDEO         = (1 << 0),
	GST_PLAY_FLAG_AUDIO         = (1 << 1),
	GST_PLAY_FLAG_TEXT          = (1 << 2),
	GST_PLAY_FLAG_VIS           = (1 << 3),
	GST_PLAY_FLAG_SOFT_VOLUME   = (1 << 4),
	GST_PLAY_FLAG_NATIVE_AUDIO  = (1 << 5),
	GST_PLAY_FLAG_NATIVE_VIDEO  = (1 << 6),
	GST_PLAY_FLAG_DOWNLOAD      = (1 << 7),
	GST_PLAY_FLAG_BUFFERING     = (1 << 8),
	GST_PLAY_FLAG_DEINTERLACE   = (1 << 9),
	GST_PLAY_FLAG_SOFT_COLORBALANCE = (1 << 10),
	GST_PLAY_FLAG_FORCE_FILTERS = (1 << 11),
} GstPlayFlags;

// eServiceFactoryMP3

/*
 * gstreamer suffers from a bug causing sparse streams to loose sync, after pause/resume / skip
 * see: https://bugzilla.gnome.org/show_bug.cgi?id=619434
 * As a workaround, we run the subsink in sync=false mode
 */
#if GST_VERSION_MAJOR < 1 
#define GSTREAMER_SUBTITLE_SYNC_MODE_BUG
#else
#undef GSTREAMER_SUBTITLE_SYNC_MODE_BUG
#endif
/**/

void mp3Blit(){
	fbClass *fb = fbClass::getInstance();
	fb->blit();
}

eServiceFactoryMP3::eServiceFactoryMP3()
{
	ePtr<eServiceCenter> sc;

	eServiceCenter::getPrivInstance(sc);
	if (sc)
	{
		std::list<std::string> extensions;
		extensions.push_back("dts");
		extensions.push_back("mp2");
		extensions.push_back("mp3");
		extensions.push_back("ogg");
		extensions.push_back("ogm");
		extensions.push_back("ogv");
		extensions.push_back("wav");
		extensions.push_back("wave");
		extensions.push_back("flac");
		extensions.push_back("flv");
		extensions.push_back("m4a");
		extensions.push_back("3gp");
		extensions.push_back("3g2");
		extensions.push_back("asf");
#if defined(__sh__)
#if !defined(ENABLE_MEDIAFWGSTREAMER) || !defined(ENABLE_LIBEPLAYER3)
/* Hellmaster1024: if both gst and eplayer3 are enabled, this is the GST service!
 * We only select the audio extensions (above), and leave the Video extensions for
 * the ePlayer3 service located in serviceeplayer3
 * If only one of GST and ePlayer3 this service handles all extensions and switches between
 * GST and ePlayer3
*/
		extensions.push_back("m4v");
		extensions.push_back("mkv");
		extensions.push_back("avi");
		extensions.push_back("divx");
		extensions.push_back("dat");
		extensions.push_back("mpg");
		extensions.push_back("vob");
		extensions.push_back("mp4");
		extensions.push_back("mov");
		extensions.push_back("mpeg");
		extensions.push_back("m2ts");
		extensions.push_back("trp");
		extensions.push_back("vdr");
		extensions.push_back("mts");
		extensions.push_back("rar");
		extensions.push_back("img");
		extensions.push_back("iso");
		extensions.push_back("ifo");
		extensions.push_back("wmv");
#endif
#endif
		extensions.push_back("wma");
		extensions.push_back("stream");
		sc->addServiceFactory(eServiceFactoryMP3::id, this, extensions);
	}

	m_service_info = new eStaticServiceMP3Info();
}

eServiceFactoryMP3::~eServiceFactoryMP3()
{
	ePtr<eServiceCenter> sc;

	eServiceCenter::getPrivInstance(sc);
	if (sc)
		sc->removeServiceFactory(eServiceFactoryMP3::id);
}

DEFINE_REF(eServiceFactoryMP3)

	// iServiceHandler
RESULT eServiceFactoryMP3::play(const eServiceReference &ref, ePtr<iPlayableService> &ptr)
{
		// check resources...
	ptr = new eServiceMP3(ref);
	return 0;
}

RESULT eServiceFactoryMP3::record(const eServiceReference &ref, ePtr<iRecordableService> &ptr)
{
	if (ref.path.find("://") != std::string::npos)
	{
		ptr = new eServiceMP3Record((eServiceReference&)ref);
		return 0;
	}
	ptr=0;
	return -1;
}

RESULT eServiceFactoryMP3::list(const eServiceReference &, ePtr<iListableService> &ptr)
{
	ptr=0;
	return -1;
}

RESULT eServiceFactoryMP3::info(const eServiceReference &ref, ePtr<iStaticServiceInformation> &ptr)
{
	ptr = m_service_info;
	return 0;
}

class eMP3ServiceOfflineOperations: public iServiceOfflineOperations
{
	DECLARE_REF(eMP3ServiceOfflineOperations);
	eServiceReference m_ref;
public:
	eMP3ServiceOfflineOperations(const eServiceReference &ref);

	RESULT deleteFromDisk(int simulate);
	RESULT getListOfFilenames(std::list<std::string> &);
	RESULT reindex();
};

DEFINE_REF(eMP3ServiceOfflineOperations);

eMP3ServiceOfflineOperations::eMP3ServiceOfflineOperations(const eServiceReference &ref): m_ref((const eServiceReference&)ref)
{
}

RESULT eMP3ServiceOfflineOperations::deleteFromDisk(int simulate)
{
	if (!simulate)
	{
		std::list<std::string> res;
		if (getListOfFilenames(res))
			return -1;

		eBackgroundFileEraser *eraser = eBackgroundFileEraser::getInstance();
		if (!eraser)
			eDebug("[eMP3ServiceOfflineOperations] FATAL !! can't get background file eraser");

		for (std::list<std::string>::iterator i(res.begin()); i != res.end(); ++i)
		{
			eDebug("[eMP3ServiceOfflineOperations] Removing %s...", i->c_str());
			if (eraser)
				eraser->erase(i->c_str());
			else
				::unlink(i->c_str());
		}
	}
	return 0;
}

RESULT eMP3ServiceOfflineOperations::getListOfFilenames(std::list<std::string> &res)
{
	res.clear();
	res.push_back(m_ref.path);
	return 0;
}

RESULT eMP3ServiceOfflineOperations::reindex()
{
	return -1;
}


RESULT eServiceFactoryMP3::offlineOperations(const eServiceReference &ref, ePtr<iServiceOfflineOperations> &ptr)
{
	ptr = new eMP3ServiceOfflineOperations(ref);
	return 0;
}

// eStaticServiceMP3Info


// eStaticServiceMP3Info is seperated from eServiceMP3 to give information
// about unopened files.

// probably eServiceMP3 should use this class as well, and eStaticServiceMP3Info
// should have a database backend where ID3-files etc. are cached.
// this would allow listing the mp3 database based on certain filters.

DEFINE_REF(eStaticServiceMP3Info)

eStaticServiceMP3Info::eStaticServiceMP3Info()
{
}

RESULT eStaticServiceMP3Info::getName(const eServiceReference &ref, std::string &name)
{
	if ( ref.name.length() )
		name = ref.name;
	else
	{
		size_t last = ref.path.rfind('/');
		if (last != std::string::npos)
			name = ref.path.substr(last+1);
		else
			name = ref.path;
	}
	return 0;
}

int eStaticServiceMP3Info::getLength(const eServiceReference &ref)
{
	return -1;
}

int eStaticServiceMP3Info::getInfo(const eServiceReference &ref, int w)
{
	switch (w)
	{
	case iServiceInformation::sTimeCreate:
		{
			struct stat s;
			if (stat(ref.path.c_str(), &s) == 0)
			{
				return s.st_mtime;
			}
		}
		break;
	case iServiceInformation::sFileSize:
		{
			struct stat s;
			if (stat(ref.path.c_str(), &s) == 0)
			{
				return s.st_size;
			}
		}
		break;
	}
	return iServiceInformation::resNA;
}

long long eStaticServiceMP3Info::getFileSize(const eServiceReference &ref)
{
	struct stat s;
	if (stat(ref.path.c_str(), &s) == 0)
	{
		return s.st_size;
	}
	return 0;
}

RESULT eStaticServiceMP3Info::getEvent(const eServiceReference &ref, ePtr<eServiceEvent> &evt, time_t start_time)
{
	if (ref.path.find("://") != std::string::npos)
	{
		eServiceReference equivalentref(ref);
		equivalentref.type = eServiceFactoryMP3::id;
		equivalentref.path.clear();
		return eEPGCache::getInstance()->lookupEventTime(equivalentref, start_time, evt);
	}
	evt = 0;
	return -1;
}

DEFINE_REF(eStreamBufferInfo)

eStreamBufferInfo::eStreamBufferInfo(int percentage, int inputrate, int outputrate, int space, int size)
: bufferPercentage(percentage),
	inputRate(inputrate),
	outputRate(outputrate),
	bufferSpace(space),
	bufferSize(size)
{
}

int eStreamBufferInfo::getBufferPercentage() const
{
	return bufferPercentage;
}

int eStreamBufferInfo::getAverageInputRate() const
{
	return inputRate;
}

int eStreamBufferInfo::getAverageOutputRate() const
{
	return outputRate;
}

int eStreamBufferInfo::getBufferSpace() const
{
	return bufferSpace;
}

int eStreamBufferInfo::getBufferSize() const
{
	return bufferSize;
}

#ifdef ENABLE_MEDIAFWGSTREAMER
DEFINE_REF(eServiceMP3InfoContainer);

eServiceMP3InfoContainer::eServiceMP3InfoContainer()
: doubleValue(0.0), bufferValue(NULL), bufferData(NULL), bufferSize(0)
{
}

eServiceMP3InfoContainer::~eServiceMP3InfoContainer()
{
	if (bufferValue)
	{
#if GST_VERSION_MAJOR >= 1
		gst_buffer_unmap(bufferValue, &map);
#endif
		gst_buffer_unref(bufferValue);
		bufferValue = NULL;
		bufferData = NULL;
		bufferSize = 0;
	}
}

double eServiceMP3InfoContainer::getDouble(unsigned int index) const
{
	return doubleValue;
}

unsigned char *eServiceMP3InfoContainer::getBuffer(unsigned int &size) const
{
	size = bufferSize;
	return bufferData;
}

void eServiceMP3InfoContainer::setDouble(double value)
{
	doubleValue = value;
}

void eServiceMP3InfoContainer::setBuffer(GstBuffer *buffer)
{
	bufferValue = buffer;
	gst_buffer_ref(bufferValue);
#if GST_VERSION_MAJOR < 1
	bufferData = GST_BUFFER_DATA(bufferValue);
	bufferSize = GST_BUFFER_SIZE(bufferValue);
#else
	gst_buffer_map(bufferValue, &map, GST_MAP_READ);
	bufferData = map.data;
	bufferSize = map.size;
#endif
}
#endif

// eServiceMP3
int eServiceMP3::ac3_delay = 0,
    eServiceMP3::pcm_delay = 0;

eServiceMP3::eServiceMP3(eServiceReference ref):
	m_nownext_timer(eTimer::create(eApp)),
	m_cuesheet_changed(0),
	m_cutlist_enabled(1),
	m_ref(ref),
	m_pump(eApp, 1)
{
	m_subtitle_sync_timer = eTimer::create(eApp);
#ifdef ENABLE_MEDIAFWGSTREAMER
	m_stream_tags = 0;
#endif
	m_currentAudioStream = -1;
	m_currentSubtitleStream = -1;
	m_cachedSubtitleStream = -2; /* report subtitle stream to be 'cached'. TODO: use an actual cache. */
	m_subtitle_widget = 0;
	m_currentTrickRatio = 1.0;
	m_buffer_size = 5 * 1024 * 1024;
#ifdef ENABLE_MEDIAFWGSTREAMER
	m_ignore_buffering_messages = 0;
	m_is_live = false;
	m_use_prefillbuffer = false;
	m_paused = false;
	m_seek_paused = false;
	m_cuesheet_loaded = false; /* cuesheet CVR */
#if GST_VERSION_MAJOR >= 1
	m_use_chapter_entries = false; /* TOC chapter support CVR */
	m_last_seek_pos = 0; /* CVR last seek position */
#endif
	m_useragent = "Enigma2 HbbTV/1.1.1 (+PVR+RTSP+DL;OpenPLi;;;)";
	m_extra_headers = "";
	m_download_buffer_path = "";
#endif
	m_prev_decoder_time = -1;
	m_decoder_time_valid_state = 0;
	m_errorInfo.missing_codec = "";
#ifdef ENABLE_MEDIAFWGSTREAMER
	audioSink = videoSink = NULL;
#endif

	CONNECT(m_subtitle_sync_timer->timeout, eServiceMP3::pushSubtitles);
#ifdef ENABLE_MEDIAFWGSTREAMER
	CONNECT(m_pump.recv_msg, eServiceMP3::gstPoll);
#endif
	CONNECT(m_nownext_timer->timeout, eServiceMP3::updateEpgCacheNowNext);
	m_aspect = m_width = m_height = m_framerate = m_progressive = -1;

	m_state = stIdle;
	eDebug("[eServiceMP3] construct!");

	const char *filename;
	std::string filename_str;
	size_t pos = m_ref.path.find('#');
	if (pos != std::string::npos && (m_ref.path.compare(0, 4, "http") == 0 || m_ref.path.compare(0, 4, "rtsp") == 0))
	{
		filename_str = m_ref.path.substr(0, pos);
		filename = filename_str.c_str();
		m_extra_headers = m_ref.path.substr(pos + 1);

		pos = m_extra_headers.find("User-Agent=");
		if (pos != std::string::npos)
		{
			size_t hpos_start = pos + 11;
			size_t hpos_end = m_extra_headers.find('&', hpos_start);
			if (hpos_end != std::string::npos)
				m_useragent = m_extra_headers.substr(hpos_start, hpos_end - hpos_start);
			else
				m_useragent = m_extra_headers.substr(hpos_start);
		}
	}
	else
		filename = m_ref.path.c_str();
	const char *ext = strrchr(filename, '.');
	if (!ext)
		ext = filename + strlen(filename);

#ifdef ENABLE_MEDIAFWGSTREAMER
	m_sourceinfo.is_video = FALSE;
	m_sourceinfo.audiotype = atUnknown;
	if ( (strcasecmp(ext, ".mpeg") && strcasecmp(ext, ".mpg") && strcasecmp(ext, ".vob") && strcasecmp(ext, ".bin") && strcasecmp(ext, ".dat") ) == 0 )
	{
		m_sourceinfo.containertype = ctMPEGPS;
		m_sourceinfo.is_video = TRUE;
	}
	else if ( strcasecmp(ext, ".ts") == 0 )
	{
		m_sourceinfo.containertype = ctMPEGTS;
		m_sourceinfo.is_video = TRUE;
	}
	else if ( strcasecmp(ext, ".mkv") == 0 )
	{
		m_sourceinfo.containertype = ctMKV;
		m_sourceinfo.is_video = TRUE;
	}
	else if ( strcasecmp(ext, ".ogm") == 0 || strcasecmp(ext, ".ogv") == 0)
	{
		m_sourceinfo.containertype = ctOGG;
		m_sourceinfo.is_video = TRUE;
	}
	else if ( strcasecmp(ext, ".avi") == 0 || strcasecmp(ext, ".divx") == 0)
	{
		m_sourceinfo.containertype = ctAVI;
		m_sourceinfo.is_video = TRUE;
	}
	else if ( strcasecmp(ext, ".mp4") == 0 || strcasecmp(ext, ".mov") == 0 || strcasecmp(ext, ".m4v") == 0 || strcasecmp(ext, ".3gp") == 0 || strcasecmp(ext, ".3g2") == 0)
	{
		m_sourceinfo.containertype = ctMP4;
		m_sourceinfo.is_video = TRUE;
	}
	else if ( strcasecmp(ext, ".asf") == 0 || strcasecmp(ext, ".wmv") == 0)
	{
		m_sourceinfo.containertype = ctASF;
		m_sourceinfo.is_video = TRUE;
	}
	else if ( strcasecmp(ext, ".m4a") == 0 )
	{
		m_sourceinfo.containertype = ctMP4;
		m_sourceinfo.audiotype = atAAC;
	}
	else if ( strcasecmp(ext, ".mp3") == 0 )
		m_sourceinfo.audiotype = atMP3;
	else if ( strcasecmp(ext, ".wma") == 0 )
		m_sourceinfo.audiotype = atWMA;
	else if ( strcasecmp(ext, ".wav") == 0 )
		m_sourceinfo.audiotype = atPCM;
	else if ( strcasecmp(ext, ".cda") == 0)
		m_sourceinfo.containertype = ctCDA;
	if ( strcasecmp(ext, ".dat") == 0 )
	{
		m_sourceinfo.containertype = ctVCD;
		m_sourceinfo.is_video = TRUE;
	}
	if ( strstr(filename, "://") )
		m_sourceinfo.is_streaming = TRUE;

	gchar *uri;

	if ( m_sourceinfo.is_streaming )
	{
		if (eConfigManager::getConfigBoolValue("config.mediaplayer.useAlternateUserAgent"))
			m_useragent = eConfigManager::getConfigValue("config.mediaplayer.alternateUserAgent");

		uri = g_strdup_printf ("%s", filename);

		if ( m_ref.getData(7) & BUFFERING_ENABLED )
		{
			m_use_prefillbuffer = true;
			if ( m_ref.getData(7) & PROGRESSIVE_DOWNLOAD )
			{
				/* progressive download buffering */
				if (::access("/hdd/movie", X_OK) >= 0)
				{
					/* It looks like /hdd points to a valid mount, so we can store a download buffer on it */
					m_download_buffer_path = "/hdd/gstreamer_XXXXXXXXXX";
				}
			}
		}
	}
	else if ( m_sourceinfo.containertype == ctCDA )
	{
		int i_track = atoi(filename+(strlen(filename) - 6));
		uri = g_strdup_printf ("cdda://%i", i_track);
	}
	else if ( m_sourceinfo.containertype == ctVCD )
	{
		int ret = -1;
		int fd = open(filename,O_RDONLY);
		if (fd >= 0)
		{
			char* tmp = new char[128*1024];
			ret = read(fd, tmp, 128*1024);
			close(fd);
			delete [] tmp;
		}
		if ( ret == -1 ) // this is a "REAL" VCD
			uri = g_strdup_printf ("vcd://");
		else
			uri = g_filename_to_uri(filename, NULL, NULL);
	}
	else
		uri = g_filename_to_uri(filename, NULL, NULL);

	eDebug("[eServiceMP3] playbin uri=%s", uri);
#if GST_VERSION_MAJOR < 1
	m_gst_playbin = gst_element_factory_make("playbin2", "playbin");
#else
	m_gst_playbin = gst_element_factory_make("playbin", "playbin");
#endif
	if ( m_gst_playbin )
	{
		/*
		 * avoid video conversion, let the dvbmediasink handle that using native video flag
		 * volume control is done by hardware, do not use soft volume flag
		 */
		guint flags = GST_PLAY_FLAG_AUDIO | GST_PLAY_FLAG_VIDEO | \
				GST_PLAY_FLAG_TEXT | GST_PLAY_FLAG_NATIVE_VIDEO;

		if ( m_sourceinfo.is_streaming )
		{
			g_signal_connect (G_OBJECT (m_gst_playbin), "notify::source", G_CALLBACK (playbinNotifySource), this);
			if (m_download_buffer_path != "")
			{
				/* use progressive download buffering */
				flags |= GST_PLAY_FLAG_DOWNLOAD;
				g_signal_connect(G_OBJECT(m_gst_playbin), "element-added", G_CALLBACK(handleElementAdded), this);
				/* limit file size */
				g_object_set(m_gst_playbin, "ring-buffer-max-size", (guint64)(8LL * 1024LL * 1024LL), NULL);
			}
			/*
			 * regardless whether or not we configured a progressive download file, use a buffer as well
			 * (progressive download might not work for all formats)
			 */
			flags |= GST_PLAY_FLAG_BUFFERING;
			/* increase the default 2 second / 2 MB buffer limitations to 5s / 5MB */
			g_object_set(G_OBJECT(m_gst_playbin), "buffer-duration", 5LL * GST_SECOND, NULL);
			g_object_set(G_OBJECT(m_gst_playbin), "buffer-size", m_buffer_size, NULL);
		}
		g_object_set (G_OBJECT (m_gst_playbin), "flags", flags, NULL);
		g_object_set (G_OBJECT (m_gst_playbin), "uri", uri, NULL);
		GstElement *subsink = gst_element_factory_make("subsink", "subtitle_sink");
		if (!subsink)
			eDebug("[eServiceMP3] sorry, can't play: missing gst-plugin-subsink");
		else
		{
			m_subs_to_pull_handler_id = g_signal_connect (subsink, "new-buffer", G_CALLBACK (gstCBsubtitleAvail), this);
#if GST_VERSION_MAJOR < 1
			g_object_set (G_OBJECT (subsink), "caps", gst_caps_from_string("text/plain; text/x-plain; text/x-raw; text/x-pango-markup; video/x-dvd-subpicture; subpicture/x-pgs"), NULL);
#else
			g_object_set (G_OBJECT (subsink), "caps", gst_caps_from_string("text/plain; text/x-plain; text/x-raw; text/x-pango-markup; subpicture/x-dvd; subpicture/x-pgs"), NULL);
#endif
			g_object_set (G_OBJECT (m_gst_playbin), "text-sink", subsink, NULL);
			g_object_set (G_OBJECT (m_gst_playbin), "current-text", m_currentSubtitleStream, NULL);
		}
		GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE (m_gst_playbin));
#if GST_VERSION_MAJOR < 1
		gst_bus_set_sync_handler(bus, gstBusSyncHandler, this);
#else
		gst_bus_set_sync_handler(bus, gstBusSyncHandler, this, NULL);
#endif
		gst_object_unref(bus);
		char srt_filename[ext - filename + 5];
		strncpy(srt_filename,filename, ext - filename);
		srt_filename[ext - filename] = '\0';
		strcat(srt_filename, ".srt");
		if (::access(srt_filename, R_OK) >= 0)
		{
			eDebug("[eServiceMP3] subtitle uri: %s", g_filename_to_uri(srt_filename, NULL, NULL));
			g_object_set (G_OBJECT (m_gst_playbin), "suburi", g_filename_to_uri(srt_filename, NULL, NULL), NULL);
		}
	} else
	{
		m_event((iPlayableService*)this, evUser+12);
		m_gst_playbin = 0;
		m_errorInfo.error_message = "failed to create GStreamer pipeline!\n";

		eDebug("[eServiceMP3] sorry, can't play: %s",m_errorInfo.error_message.c_str());
	}
	g_free(uri);
#else
	player = (Context_t*) malloc(sizeof(Context_t));

	if (player)
	{
		player->playback  = &PlaybackHandler;
		player->output    = &OutputHandler;
		player->container = &ContainerHandler;
		player->manager   = &ManagerHandler;
		printf("%s\n", player->output->Name);
	}

	//Registration of output devices
	if (player && player->output)
	{
		player->output->Command(player,OUTPUT_ADD, (void*)"audio");
		player->output->Command(player,OUTPUT_ADD, (void*)"video");
		player->output->Command(player,OUTPUT_ADD, (void*)"subtitle");
	}

	if (player && player->output && player->output->subtitle)
	{
		fbClass *fb = fbClass::getInstance();
		SubtitleOutputDef_t out;
		out.screen_width = fb->getScreenResX();
		out.screen_height = fb->getScreenResY();
		out.shareFramebuffer = 1;
		out.framebufferFD = fb->getFD();
		out.destination = fb->getLFB_Direct();
		out.destStride = fb->Stride();
		out.framebufferBlit = mp3Blit;
		player->output->subtitle->Command(player, (OutputCmd_t)OUTPUT_SET_SUBTITLE_OUTPUT, (void*) &out);
	}

	//create playback path
	char file[800] = {""};

	if (!strncmp("http://", m_ref.path.c_str(), 7))
		;
	else if (!strncmp("rtsp://", m_ref.path.c_str(), 7))
		;
	else if (!strncmp("rtmp://", m_ref.path.c_str(), 7))
		;
	else if (!strncmp("rtmpe://", m_ref.path.c_str(), 8))
		;
	else if (!strncmp("rtmpt://", m_ref.path.c_str(), 8))
		;
	else if (!strncmp("rtmps://", m_ref.path.c_str(), 8))
		;
	else if (!strncmp("rtmpte://", m_ref.path.c_str(), 9))
		;
	else if (!strncmp("rtp://", m_ref.path.c_str(), 6))
		;
	else if (!strncmp("upnp://", m_ref.path.c_str(), 7))
		;
	else if (!strncmp("mms://", m_ref.path.c_str(), 6))
		;
	else if (!strncmp("file://", m_ref.path.c_str(), 7))
		;
	else
		strcat(file, "file://");
	strcat(file, m_ref.path.c_str());

	//try to open file
	if (player && player->playback && player->playback->Command(player, PLAYBACK_OPEN, file) >= 0)
	{
		//VIDEO
		//We dont have to register video tracks, or do we ?
		//AUDIO
		if (player && player->manager && player->manager->audio)
		{
			char ** TrackList = NULL;
			player->manager->audio->Command(player, MANAGER_LIST, &TrackList);
			if (TrackList != NULL)
			{
				printf("AudioTrack List\n");
				int i = 0;
				for (i = 0; TrackList[i] != NULL; i+=2)
				{
					printf("\t%s - %s\n", TrackList[i], TrackList[i+1]);
					audioStream audio;
					audio.language_code = TrackList[i];

					// atUnknown, atMPEG, atMP3, atAC3, atDTS, atAAC, atPCM, atOGG, atFLAC
					if (    !strncmp("A_MPEG/L3",   TrackList[i+1], 9))
						audio.type = atMP3;
					else if (!strncmp("A_MP3",      TrackList[i+1], 5))
						audio.type = atMP3;
					else if (!strncmp("A_AC3",      TrackList[i+1], 5))
						audio.type = atAC3;
					else if (!strncmp("A_DTS",      TrackList[i+1], 5))
						audio.type = atDTS;
					else if (!strncmp("A_AAC",      TrackList[i+1], 5))
						audio.type = atAAC;
					else if (!strncmp("A_PCM",      TrackList[i+1], 5))
						audio.type = atPCM;
					else if (!strncmp("A_VORBIS",   TrackList[i+1], 8))
						audio.type = atOGG;
					else if (!strncmp("A_FLAC",     TrackList[i+1], 6))
						audio.type = atFLAC;
					else
						audio.type = atUnknown;

					m_audioStreams.push_back(audio);
					free(TrackList[i]);
					free(TrackList[i+1]);
				}
				free(TrackList);
			}
		}
		//SUB
		if (player && player->manager && player->manager->subtitle)
		{
			char ** TrackList = NULL;
			player->manager->subtitle->Command(player, MANAGER_LIST, &TrackList);
			if (TrackList != NULL)
			{
				printf("SubtitleTrack List\n");
				int i = 0;
				for (i = 0; TrackList[i] != NULL; i+=2)
				{
					printf("\t%s - %s\n", TrackList[i], TrackList[i+1]);
					subtitleStream sub;
					sub.language_code = TrackList[i];
					//  stPlainText, stSSA, stSRT
					if (    !strncmp("S_TEXT/SSA",   TrackList[i+1], 10) ||
							!strncmp("S_SSA", TrackList[i+1], 5))
						sub.type = stSSA;
					else if (!strncmp("S_TEXT/ASS",   TrackList[i+1], 10) ||
							!strncmp("S_AAS", TrackList[i+1], 5))
						sub.type = stSSA;
					else if (!strncmp("S_TEXT/SRT",   TrackList[i+1], 10) ||
							!strncmp("S_SRT", TrackList[i+1], 5))
						sub.type = stSRT;
					else
						sub.type = stPlainText;

					m_subtitleStreams.push_back(sub);
					free(TrackList[i]);
					free(TrackList[i+1]);
				}
				free(TrackList);
			}
		}
		m_event(this, evStart);
	}
	else
	{
		//Creation failed, no playback support for insert file, so delete playback context
		//FIXME: How to tell e2 that we failed?
		if (player && player->output)
		{
			player->output->Command(player,OUTPUT_DEL, (void*)"audio");
			player->output->Command(player,OUTPUT_DEL, (void*)"video");
			player->output->Command(player,OUTPUT_DEL, (void*)"subtitle");
		}

		if (player && player->playback)
			player->playback->Command(player,PLAYBACK_CLOSE, NULL);

		if (player)
			free(player);
		player = NULL;
	}
	//m_state = stRunning;
	eDebug("eServiceMP3-<\n");
#endif
}

eServiceMP3::~eServiceMP3()
{
#ifdef ENABLE_MEDIAFWGSTREAMER
	// disconnect subtitle callback
	GstElement *subsink = gst_bin_get_by_name(GST_BIN(m_gst_playbin), "subtitle_sink");

	if (subsink)
	{
		g_signal_handler_disconnect (subsink, m_subs_to_pull_handler_id);
		gst_object_unref(subsink);
	}
#endif

	if (m_subtitle_widget) m_subtitle_widget->destroy();
	m_subtitle_widget = 0;

#ifdef ENABLE_MEDIAFWGSTREAMER
	if (m_gst_playbin)
	{
		// disconnect sync handler callback
		GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE (m_gst_playbin));
#if GST_VERSION_MAJOR < 1
		gst_bus_set_sync_handler(bus, NULL, NULL);
#else
		gst_bus_set_sync_handler(bus, NULL, NULL, NULL);
#endif
		gst_object_unref(bus);
	}
#else
	if (m_state == stRunning)
#endif
		stop();

#ifdef ENABLE_MEDIAFWGSTREAMER
	if (m_stream_tags)
		gst_tag_list_free(m_stream_tags);

	if (audioSink)
	{
		gst_object_unref(GST_OBJECT(audioSink));
		audioSink = NULL;
	}
	if (videoSink)
	{
		gst_object_unref(GST_OBJECT(videoSink));
		videoSink = NULL;
	}
	if (m_gst_playbin)
	{
		gst_object_unref (GST_OBJECT (m_gst_playbin));
		eDebug("[eServiceMP3] destruct!");
	}
#endif
}

void eServiceMP3::updateEpgCacheNowNext()
{
	bool update = false;
	ePtr<eServiceEvent> next = 0;
	ePtr<eServiceEvent> ptr = 0;
	eServiceReference ref(m_ref);
	ref.type = eServiceFactoryMP3::id;
	ref.path.clear();
	if (eEPGCache::getInstance() && eEPGCache::getInstance()->lookupEventTime(ref, -1, ptr) >= 0)
	{
		ePtr<eServiceEvent> current = m_event_now;
		if (!current || !ptr || current->getEventId() != ptr->getEventId())
		{
			update = true;
			m_event_now = ptr;
			time_t next_time = ptr->getBeginTime() + ptr->getDuration();
			if (eEPGCache::getInstance()->lookupEventTime(ref, next_time, ptr) >= 0)
			{
				next = ptr;
				m_event_next = ptr;
			}
		}
	}

	int refreshtime = 60;
	if (!next)
	{
		next = m_event_next;
	}
	if (next)
	{
		time_t now = eDVBLocalTimeHandler::getInstance()->nowTime();
		refreshtime = (int)(next->getBeginTime() - now) + 3;
		if (refreshtime <= 0 || refreshtime > 60)
		{
			refreshtime = 60;
		}
	}
	m_nownext_timer->startLongTimer(refreshtime);
	if (update)
	{
		m_event((iPlayableService*)this, evUpdatedEventInfo);
	}
}

DEFINE_REF(eServiceMP3);

#ifdef ENABLE_MEDIAFWGSTREAMER
DEFINE_REF(GstMessageContainer);
#endif

RESULT eServiceMP3::connectEvent(const Slot2<void,iPlayableService*,int> &event, ePtr<eConnection> &connection)
{
	connection = new eConnection((iPlayableService*)this, m_event.connect(event));
#ifdef ENABLE_MEDIAFWGSTREAMER
#else
	m_event(this, evSeekableStatusChanged);
#endif
	return 0;
}

RESULT eServiceMP3::start()
{
#ifdef ENABLE_MEDIAFWGSTREAMER
	ASSERT(m_state == stIdle);
#else
	if (m_state != stIdle)
	{
		eDebug("eServiceMP3::%s < m_state != stIdle", __func__);
		return -1;
	}

	m_state = stRunning;
#endif

#ifdef ENABLE_MEDIAFWGSTREAMER
	if (m_gst_playbin)
	{
		eDebug("[eServiceMP3] starting pipeline");
		GstStateChangeReturn ret;
		ret = gst_element_set_state (m_gst_playbin, GST_STATE_PLAYING);

		switch(ret)
		{
		case GST_STATE_CHANGE_FAILURE:
			eDebug("[eServiceMP3] failed to start pipeline");
			stop();
			break;
		case GST_STATE_CHANGE_SUCCESS:
			m_is_live = false;
			break;
		case GST_STATE_CHANGE_NO_PREROLL:
			m_is_live = true;
			break;
		default:
			break;
		}
	}
#else
	if (player && player->output && player->playback)
	{
		player->output->Command(player, OUTPUT_OPEN, NULL);
		player->playback->Command(player, PLAYBACK_PLAY, NULL);
	}

	m_event(this, evStart);
#endif

	return 0;
}

#ifdef ENABLE_MEDIAFWGSTREAMER
#else
void eServiceMP3::sourceTimeout()
{
	eDebug("[eServiceMP3] http source timeout! issuing eof...");
	m_event((iPlayableService*)this, evEOF);
}
#endif

RESULT eServiceMP3::stop()
{
#ifdef ENABLE_MEDIAFWGSTREAMER
	if (!m_gst_playbin || m_state == stStopped)
		return -1;
#else
	if (m_state == stIdle)
	{
		eDebug("eServiceMP3::%s < m_state == stIdle", __func__);
		return -1;
	}

	if (m_state == stStopped)
		return -1;

	if (player && player->playback && player->output)
	{
		player->playback->Command(player, PLAYBACK_STOP, NULL);
		player->output->Command(player, OUTPUT_CLOSE, NULL);
	}

	if (player && player->output)
	{
		player->output->Command(player,OUTPUT_DEL, (void*)"audio");
		player->output->Command(player,OUTPUT_DEL, (void*)"video");
		player->output->Command(player,OUTPUT_DEL, (void*)"subtitle");
	}

	if (player && player->playback)
		player->playback->Command(player,PLAYBACK_CLOSE, NULL);

	if (player)
		free(player);

	if (player != NULL)
		player = NULL;
#endif
//	eDebug("[eServiceMP3] stop %s", m_ref.path.c_str());
	m_state = stStopped;

#ifdef ENABLE_MEDIAFWGSTREAMER
	GstStateChangeReturn ret;
	GstState state, pending;
	/* make sure that last state change was successfull */
	ret = gst_element_get_state(m_gst_playbin, &state, &pending, 5 * GST_SECOND);
	eDebug("[eServiceMP3] stop state:%s pending:%s ret:%s",
		gst_element_state_get_name(state),
		gst_element_state_get_name(pending),
		gst_element_state_change_return_get_name(ret));

	ret = gst_element_set_state(m_gst_playbin, GST_STATE_NULL);
	if (ret != GST_STATE_CHANGE_SUCCESS)
		eDebug("[eServiceMP3] stop GST_STATE_NULL failure");
#endif
	saveCuesheet();
	m_nownext_timer->stop();
#ifdef ENABLE_MEDIAFWGSTREAMER
#else
	if (m_streamingsrc_timeout)
		m_streamingsrc_timeout->stop();
#endif

	return 0;
}

RESULT eServiceMP3::setTarget(int target)
{
	return -1;
}

RESULT eServiceMP3::pause(ePtr<iPauseableService> &ptr)
{
	ptr=this;
	return 0;
}

#ifdef ENABLE_MEDIAFWGSTREAMER
#else
int speed_mapping[] =
{
 /* e2_ratio   speed */
	2,         1,
	4,         3,
	8,         7,
	16,        15,
	32,        31,
	64,        63,
	128,      127,
	-2,       -5,
	-4,      -10,
	-8,      -20,
	-16,      -40,
	-32,      -80,
	-64,     -160,
	-128,     -320,
	-1,       -1
};

int getSpeed(int ratio)
{
	int i = 0;
	while (speed_mapping[i] != -1)
	{
		if (speed_mapping[i] == ratio)
			return speed_mapping[i+1];
		i += 2;
	}
	return -1;
}
#endif

RESULT eServiceMP3::setSlowMotion(int ratio)
{
#ifdef ENABLE_MEDIAFWGSTREAMER
	if (!ratio)
		return 0;
	eDebug("[eServiceMP3] setSlowMotion ratio=%f",1.0/(gdouble)ratio);
	return trickSeek(1.0/(gdouble)ratio);
#else
// konfetti: in libeplayer3 we changed this because I dont like application specific stuff in a library
	int speed = getSpeed(ratio);
	if (player && player->playback && (speed != -1))
	{
		int result = 0;
		if (ratio > 1)
			result = player->playback->Command(player, PLAYBACK_SLOWMOTION, (void*)&speed);

		if (result != 0)
			return -1;
	}
	return 0;
#endif
}

RESULT eServiceMP3::setFastForward(int ratio)
{
#ifdef ENABLE_MEDIAFWGSTREAMER
//	eDebug("[eServiceMP3] setFastForward ratio=%i",ratio);
	return trickSeek(ratio);
#else
// konfetti: in libeplayer3 we changed this because I dont like application specific stuff in a library
	int speed = getSpeed(ratio);
	if (player && player->playback && (speed != -1))
	{
		int result = 0;
		if (ratio > 1)
			result = player->playback->Command(player, PLAYBACK_FASTFORWARD, (void*)&speed);
		else if (ratio < -1)
		{
			//speed = speed * -1;
			result = player->playback->Command(player, PLAYBACK_FASTBACKWARD, (void*)&speed);
		}
		else
			result = player->playback->Command(player, PLAYBACK_CONTINUE, NULL);

		if (result != 0)
			return -1;
	}
	return 0;
#endif
}

		// iPausableService
RESULT eServiceMP3::pause()
{
#ifdef ENABLE_MEDIAFWGSTREAMER
	if (!m_gst_playbin || m_state != stRunning)
		return -1;

	eDebug("[eServiceMP3] pause");
	trickSeek(0.0);
#else
	if (player && player->playback)
		player->playback->Command(player, PLAYBACK_PAUSE, NULL);
#endif

	return 0;
}

RESULT eServiceMP3::unpause()
{
#ifdef ENABLE_MEDIAFWGSTREAMER
	if (!m_gst_playbin || m_state != stRunning)
		return -1;

	/* no need to unpase if we are not paused already */
	if (m_currentTrickRatio == 1.0 && !m_paused)
	{
		eDebug("[eServiceMP3] trickSeek no need to unpause!");
		return 0;
	}

	eDebug("[eServiceMP3] unpause");
	trickSeek(1.0);
#else
	if (player && player->playback)
		player->playback->Command(player, PLAYBACK_CONTINUE, NULL);
#endif

	return 0;
}

	/* iSeekableService */
RESULT eServiceMP3::seek(ePtr<iSeekableService> &ptr)
{
	ptr = this;
	return 0;
}

RESULT eServiceMP3::getLength(pts_t &pts)
{
#ifdef ENABLE_MEDIAFWGSTREAMER
	if (!m_gst_playbin || m_state != stRunning)
		return -1;

	GstFormat fmt = GST_FORMAT_TIME;
	gint64 len;
#if GST_VERSION_MAJOR < 1
	if (!gst_element_query_duration(m_gst_playbin, &fmt, &len))
#else
	if (!gst_element_query_duration(m_gst_playbin, fmt, &len))
#endif
		return -1;
		/* len is in nanoseconds. we have 90 000 pts per second. */

	pts = len / 11111LL;
#else
	double length = 0;

	if (player && player->playback)
		player->playback->Command(player, PLAYBACK_LENGTH, &length);

	if (length <= 0)
		return -1;

	pts = length * 90000;
#endif
	return 0;
}

RESULT eServiceMP3::seekToImpl(pts_t to)
{
#ifdef ENABLE_MEDIAFWGSTREAMER
		/* convert pts to nanoseconds */
#if GST_VERSION_MAJOR < 1
	gint64 time_nanoseconds = to * 11111LL;
	if (!gst_element_seek (m_gst_playbin, m_currentTrickRatio, GST_FORMAT_TIME, (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
		GST_SEEK_TYPE_SET, time_nanoseconds,
		GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE))
#else
	m_last_seek_pos = to * 11111LL;
	if (!gst_element_seek (m_gst_playbin, m_currentTrickRatio, GST_FORMAT_TIME, (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
		GST_SEEK_TYPE_SET, m_last_seek_pos,
		GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE))
#endif
	{
		eDebug("[eServiceMP3] seekTo failed");
		return -1;
	}

	if (m_paused)
	{
#if GST_VERSION_MAJOR < 1
		m_seek_paused = true;
		gst_element_set_state(m_gst_playbin, GST_STATE_PLAYING);
#else
		m_event((iPlayableService*)this, evUpdatedInfo);
#endif
	}

	return 0;
#endif
}

RESULT eServiceMP3::seekTo(pts_t to)
{
	RESULT ret = -1;

#ifdef ENABLE_MEDIAFWGSTREAMER
	if (m_gst_playbin)
	{
		m_prev_decoder_time = -1;
		m_decoder_time_valid_state = 0;
		ret = seekToImpl(to);
	}
#else
	float pos = (to/90000.0)-10;
	if (player && player->playback)
		player->playback->Command(player, PLAYBACK_SEEK, (void*)&pos);

	ret =0;
#endif
	return ret;
}


#ifdef ENABLE_MEDIAFWGSTREAMER
RESULT eServiceMP3::trickSeek(gdouble ratio)
{
	if (!m_gst_playbin)
		return -1;
	if (ratio > -0.01 && ratio < 0.01)
	{
		gst_element_set_state(m_gst_playbin, GST_STATE_PAUSED);
		return 0;
	}

#if GST_VERSION_MAJOR >= 1
	bool unpause = (m_currentTrickRatio == 1.0 && ratio == 1.0);
	if (unpause)
	{
		GstElement *source = NULL;
		GstElementFactory *factory = NULL;
		const gchar *name = NULL;
		g_object_get (G_OBJECT (m_gst_playbin), "source", &source, NULL);
		if (!source)
		{
			eDebugNoNewLineStart("[eServiceMP3] trickSeek - cannot get source");
			goto seek_unpause;
		}
		factory = gst_element_get_factory(source);
		g_object_unref(source);
		if (!factory)
		{
			eDebugNoNewLineStart("[eServiceMP3] trickSeek - cannot get source factory");
			goto seek_unpause;
		}
		name = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));
		if (!name)
		{
			eDebugNoNewLineStart("[eServiceMP3] trickSeek - cannot get source name");
			goto seek_unpause;
		}
		/*
		 * We know that filesrc and souphttpsrc will not timeout after long pause
		 * If there are other sources which will not timeout, add them here
		*/
		if (!strcmp(name, "filesrc") || !strcmp(name, "souphttpsrc"))
		{
			GstStateChangeReturn ret;
			GstState state, pending;
			/* make sure that last state change was successfull */
			ret = gst_element_get_state(m_gst_playbin, &state, &pending, 0);
			if (ret == GST_STATE_CHANGE_SUCCESS)
			{
				gst_element_set_state(m_gst_playbin, GST_STATE_PLAYING);
				ret = gst_element_get_state(m_gst_playbin, &state, &pending, 0);
				if (ret == GST_STATE_CHANGE_SUCCESS)
					return 0;
			}
			eDebugNoNewLineStart("[eServiceMP3] trickSeek - invalid state, state:%s pending:%s ret:%s",
				gst_element_state_get_name(state),
				gst_element_state_get_name(pending),
				gst_element_state_change_return_get_name(ret));
		}
		else
		{
			eDebugNoNewLineStart("[eServiceMP3] trickSeek - source '%s' is not supported", name);
		}
seek_unpause:
		eDebugNoNewLine(", doing seeking unpause\n");
	}
#endif
	m_currentTrickRatio = ratio;

	bool validposition = false;
	gint64 pos = 0;
	pts_t pts;
	if (getPlayPosition(pts) >= 0)
	{
		validposition = true;
		pos = pts * 11111LL;
	}

	gst_element_set_state(m_gst_playbin, GST_STATE_PLAYING);

	if (validposition)
	{
		if (ratio >= 0.0)
		{
			gst_element_seek(m_gst_playbin, ratio, GST_FORMAT_TIME, (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_SKIP), GST_SEEK_TYPE_SET, pos, GST_SEEK_TYPE_SET, -1);
		}
		else
		{
			/* note that most elements will not support negative speed */
			gst_element_seek(m_gst_playbin, ratio, GST_FORMAT_TIME, (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_SKIP), GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, pos);
		}
	}

	m_prev_decoder_time = -1;
	m_decoder_time_valid_state = 0;
	return 0;
}
#endif


RESULT eServiceMP3::seekRelative(int direction, pts_t to)
{
#ifdef ENABLE_MEDIAFWGSTREAMER
	if (!m_gst_playbin)
		return -1;
#endif
	pts_t ppos;
	if (getPlayPosition(ppos) < 0) return -1;
	ppos += to * direction;
	if (ppos < 0)
		ppos = 0;
#ifdef ENABLE_MEDIAFWGSTREAMER
	return seekTo(ppos);
#else
	float pos = direction*(to/90000.0);
	if (player && player->playback)
		player->playback->Command(player, PLAYBACK_SEEK, (void*)&pos);

	return 0;
#endif
}

#ifdef ENABLE_MEDIAFWGSTREAMER
#if GST_VERSION_MAJOR < 1
gint eServiceMP3::match_sinktype(GstElement *element, gpointer type)
{
	return strcmp(g_type_name(G_OBJECT_TYPE(element)), (const char*)type);
}
#else
gint eServiceMP3::match_sinktype(const GValue *velement, const gchar *type)
{
	GstElement *element = GST_ELEMENT_CAST(g_value_get_object(velement));
	return strcmp(g_type_name(G_OBJECT_TYPE(element)), type);
}
#endif
#endif

#if HAVE_AMLOGIC
GstElement *getAVDecElement(GstElement *m_gst_playbin, int i, int flag)
{
	GstPad *pad = NULL;
	GstPad *dec_pad = NULL;
	GstElement *e = NULL;

	g_signal_emit_by_name(m_gst_playbin, flag ? "get-video-pad" : "get-audio-pad", i, &pad);
	if (pad) {
		dec_pad = gst_pad_get_peer(pad);
		while (dec_pad && GST_IS_GHOST_PAD(dec_pad)) {
			gst_object_unref(dec_pad);
			dec_pad = gst_ghost_pad_get_target(GST_GHOST_PAD(dec_pad));
		}
		if (dec_pad) {
			e = gst_pad_get_parent_element(dec_pad);
			gst_object_unref(dec_pad);
		}
		gst_object_unref(pad);
	}

	if (!e)
		eDebug("[eServiceMP3] no %sDecElement", flag ? "Video" : "Audio");

	return e;
}

void eServiceMP3::AmlSwitchAudio(int index)
{
	gint i, n_audio = 0;
	gint32 videonum = 0;
	GstElement * adec = NULL, *vdec = NULL;

	g_object_get(G_OBJECT (m_gst_playbin), "n-audio", &n_audio, NULL);
	for (i = 0; i < n_audio; i++) {
		adec = getAVDecElement(m_gst_playbin, i, 0);
		if (adec) {
			g_object_set(G_OBJECT(adec), "pass-through", TRUE, NULL);
			gst_object_unref(adec);
		}
	}
	adec = getAVDecElement(m_gst_playbin, index, 0);
	if (adec) {
		g_object_set(G_OBJECT(adec), "pass-through", FALSE, NULL);
		gst_object_unref(adec);
	}
	g_object_get(G_OBJECT (m_gst_playbin), "current-video", &videonum, NULL);
	vdec = getAVDecElement(m_gst_playbin, videonum, 1);
	if (vdec)
		g_object_set(G_OBJECT(vdec), "pass-through", TRUE, NULL);
}

unsigned int eServiceMP3::get_pts_pcrscr(void)
{
	int handle;
	int size;
	char s[16];
	unsigned int value = 0;

	handle = open("/sys/class/tsync/pts_pcrscr", O_RDONLY);
	if (handle < 0)
		return value;

	size = read(handle, s, sizeof(s));
	if (size > 0)
		value = strtoul(s, NULL, 16);
	close(handle);
	return value;
}
#endif

RESULT eServiceMP3::getPlayPosition(pts_t &pts)
{
#ifdef ENABLE_MEDIAFWGSTREAMER
	gint64 pos;
	pts = 0;

	if (!m_gst_playbin || m_state != stRunning)
		return -1;

#if HAVE_AMLOGIC
	if ((pos = get_pts_pcrscr()) > 0)
		pos *= 11111LL;
#else
#if GST_VERSION_MAJOR < 1
	if (audioSink || videoSink)
#else
	if ((audioSink || videoSink) && !m_paused)
#endif
	{
		g_signal_emit_by_name(audioSink ? audioSink : videoSink, "get-decoder-time", &pos);
		if (!GST_CLOCK_TIME_IS_VALID(pos)) return -1;
	}
#endif
	else
	{
		GstFormat fmt = GST_FORMAT_TIME;
#if GST_VERSION_MAJOR < 1
		if (!gst_element_query_position(m_gst_playbin, &fmt, &pos))
#else
		if (!gst_element_query_position(m_gst_playbin, fmt, &pos))
#endif
		{
			eDebug("[eServiceMP3] gst_element_query_position failed in getPlayPosition");
			return -1;
		}
	}

	/* pos is in nanoseconds. we have 90 000 pts per second. */
	pts = pos / 11111LL;
#else
	if (player && player->playback && !player->playback->isPlaying)
	{
		eDebug("eServiceMP3::%s !!!!EOF!!!! < -1", __func__);
		if(m_state == stRunning)
			m_event((iPlayableService*)this, evEOF);
		pts = 0;
		return -1;
	}

	unsigned long long int vpts = 0;
	if (player && player->playback)
		player->playback->Command(player, PLAYBACK_PTS, &vpts);

	if (vpts<=0)
		return -1;

	/* len is in nanoseconds. we have 90 000 pts per second. */
	pts = vpts>0?vpts:pts;;

#endif
	return 0;
}

RESULT eServiceMP3::setTrickmode(int trick)
{
		/* trickmode is not yet supported by our dvbmediasinks. */
	return -1;
}

RESULT eServiceMP3::isCurrentlySeekable()
{
#ifdef ENABLE_MEDIAFWGSTREAMER // Hellmaster1024: 1 for skipping 3 for skipping anf fast forward
	int ret = 3; /* just assume that seeking and fast/slow winding are possible */

	if (!m_gst_playbin)
		return 0;

	return ret;
#else
	return 3;
#endif
}

RESULT eServiceMP3::info(ePtr<iServiceInformation>&i)
{
	i = this;
	return 0;
}

RESULT eServiceMP3::getName(std::string &name)
{
	std::string title = m_ref.getName();
	if (title.empty())
	{
		name = m_ref.path;
		size_t n = name.rfind('/');
		if (n != std::string::npos)
			name = name.substr(n + 1);
	}
	else
		name = title;
	return 0;
}

RESULT eServiceMP3::getEvent(ePtr<eServiceEvent> &evt, int nownext)
{
	evt = nownext ? m_event_next : m_event_now;
	if (!evt)
		return -1;
	return 0;
}

int eServiceMP3::getInfo(int w)
{
#ifdef ENABLE_MEDIAFWGSTREAMER
	const gchar *tag = 0;
#endif

	switch (w)
	{
	case sServiceref: return m_ref;
	case sVideoHeight: return m_height;
	case sVideoWidth: return m_width;
	case sFrameRate: return m_framerate;
	case sProgressive: return m_progressive;
	case sAspect: return m_aspect;
	case sTagTitle:
	case sTagArtist:
	case sTagAlbum:
	case sTagTitleSortname:
	case sTagArtistSortname:
	case sTagAlbumSortname:
	case sTagDate:
	case sTagComposer:
	case sTagGenre:
	case sTagComment:
	case sTagExtendedComment:
	case sTagLocation:
	case sTagHomepage:
	case sTagDescription:
	case sTagVersion:
	case sTagISRC:
	case sTagOrganization:
	case sTagCopyright:
	case sTagCopyrightURI:
	case sTagContact:
	case sTagLicense:
	case sTagLicenseURI:
	case sTagCodec:
	case sTagAudioCodec:
	case sTagVideoCodec:
	case sTagEncoder:
	case sTagLanguageCode:
	case sTagKeywords:
	case sTagChannelMode:
	case sUser+12:
#if not defined(__sh__)
		return resIsString;
#endif
	case sTagTrackGain:
	case sTagTrackPeak:
	case sTagAlbumGain:
	case sTagAlbumPeak:
	case sTagReferenceLevel:
	case sTagBeatsPerMinute:
	case sTagImage:
	case sTagPreviewImage:
	case sTagAttachment:
		return resIsPyObject;
#ifdef ENABLE_MEDIAFWGSTREAMER
	case sTagTrackNumber:
		tag = GST_TAG_TRACK_NUMBER;
		break;
	case sTagTrackCount:
		tag = GST_TAG_TRACK_COUNT;
		break;
	case sTagAlbumVolumeNumber:
		tag = GST_TAG_ALBUM_VOLUME_NUMBER;
		break;
	case sTagAlbumVolumeCount:
		tag = GST_TAG_ALBUM_VOLUME_COUNT;
		break;
	case sTagBitrate:
		tag = GST_TAG_BITRATE;
		break;
	case sTagNominalBitrate:
		tag = GST_TAG_NOMINAL_BITRATE;
		break;
	case sTagMinimumBitrate:
		tag = GST_TAG_MINIMUM_BITRATE;
		break;
	case sTagMaximumBitrate:
		tag = GST_TAG_MAXIMUM_BITRATE;
		break;
	case sTagSerial:
		tag = GST_TAG_SERIAL;
		break;
	case sTagEncoderVersion:
		tag = GST_TAG_ENCODER_VERSION;
		break;
	case sTagCRC:
		tag = "has-crc";
		break;
#endif
	case sBuffer: return m_bufferInfo.bufferPercent;
	default:
		return resNA;
	}

#ifdef ENABLE_MEDIAFWGSTREAMER
	if (!m_stream_tags || !tag)
		return 0;

	guint value;
	if (gst_tag_list_get_uint(m_stream_tags, tag, &value))
		return (int) value;
#endif

	return 0;
}

std::string eServiceMP3::getInfoString(int w)
{
	if ( m_sourceinfo.is_streaming )
	{
		switch (w)
		{
		case sProvider:
			return "IPTV";
		case sServiceref:
		{
			eServiceReference ref(m_ref);
			ref.type = eServiceFactoryMP3::id;
			ref.path.clear();
			return ref.toString();
		}
		default:
			break;
		}
	}

#ifdef ENABLE_MEDIAFWGSTREAMER
	if ( !m_stream_tags && w < sUser && w > 26 )
		return "";
	const gchar *tag = 0;
	switch (w)
	{
	case sTagTitle:
		tag = GST_TAG_TITLE;
		break;
	case sTagArtist:
		tag = GST_TAG_ARTIST;
		break;
	case sTagAlbum:
		tag = GST_TAG_ALBUM;
		break;
	case sTagTitleSortname:
		tag = GST_TAG_TITLE_SORTNAME;
		break;
	case sTagArtistSortname:
		tag = GST_TAG_ARTIST_SORTNAME;
		break;
	case sTagAlbumSortname:
		tag = GST_TAG_ALBUM_SORTNAME;
		break;
	case sTagDate:
		GDate *date;
		GstDateTime *date_time;
		if (gst_tag_list_get_date(m_stream_tags, GST_TAG_DATE, &date))
		{
			gchar res[5];
			snprintf(res, sizeof(res), "%04d", g_date_get_year(date));
			g_date_free(date);
			return (std::string)res;
		}
#if GST_VERSION_MAJOR >= 1
		else if (gst_tag_list_get_date_time(m_stream_tags, GST_TAG_DATE_TIME, &date_time))
		{
			if (gst_date_time_has_year(date_time))
			{
				gchar res[5];
				snprintf(res, sizeof(res), "%04d", gst_date_time_get_year(date_time));
				gst_date_time_unref(date_time);
				return (std::string)res;
			}
			gst_date_time_unref(date_time);
		}
#endif
		break;
	case sTagComposer:
		tag = GST_TAG_COMPOSER;
		break;
	case sTagGenre:
		tag = GST_TAG_GENRE;
		break;
	case sTagComment:
		tag = GST_TAG_COMMENT;
		break;
	case sTagExtendedComment:
		tag = GST_TAG_EXTENDED_COMMENT;
		break;
	case sTagLocation:
		tag = GST_TAG_LOCATION;
		break;
	case sTagHomepage:
		tag = GST_TAG_HOMEPAGE;
		break;
	case sTagDescription:
		tag = GST_TAG_DESCRIPTION;
		break;
	case sTagVersion:
		tag = GST_TAG_VERSION;
		break;
	case sTagISRC:
		tag = GST_TAG_ISRC;
		break;
	case sTagOrganization:
		tag = GST_TAG_ORGANIZATION;
		break;
	case sTagCopyright:
		tag = GST_TAG_COPYRIGHT;
		break;
	case sTagCopyrightURI:
		tag = GST_TAG_COPYRIGHT_URI;
		break;
	case sTagContact:
		tag = GST_TAG_CONTACT;
		break;
	case sTagLicense:
		tag = GST_TAG_LICENSE;
		break;
	case sTagLicenseURI:
		tag = GST_TAG_LICENSE_URI;
		break;
	case sTagCodec:
		tag = GST_TAG_CODEC;
		break;
	case sTagAudioCodec:
		tag = GST_TAG_AUDIO_CODEC;
		break;
	case sTagVideoCodec:
		tag = GST_TAG_VIDEO_CODEC;
		break;
	case sTagEncoder:
		tag = GST_TAG_ENCODER;
		break;
	case sTagLanguageCode:
		tag = GST_TAG_LANGUAGE_CODE;
		break;
	case sTagKeywords:
		tag = GST_TAG_KEYWORDS;
		break;
	case sTagChannelMode:
		tag = "channel-mode";
		break;
	case sUser+12:
		return m_errorInfo.error_message;
	default:
		return "";
	}
	if ( !tag )
		return "";
	gchar *value = NULL;
	if (m_stream_tags && gst_tag_list_get_string(m_stream_tags, tag, &value))
	{
		std::string res = value;
		g_free(value);
		return res;
	}
#else
	char * tag = NULL;
	char * res_str = NULL;
	switch (w)
	{
	case sTagTitle:
		tag = strdup("Title");
		break;
	case sTagArtist:
		tag = strdup("Artist");
		break;
	case sTagAlbum:
		tag = strdup("Album");
		break;
	case sTagComment:
		tag = strdup("Comment");
		break;
	case sTagTrackNumber:
		tag = strdup("Track");
		break;
	case sTagGenre:
		tag = strdup("Genre");
		break;
	case sTagDate:
		tag = strdup("Year");
		break;
	case sTagVideoCodec:
		tag = strdup("VideoType");
		break;
	case sTagAudioCodec:
		tag = strdup("AudioType");
		break;
	default:
		return "";
	}

	if (player && player->playback)
	{
		/*Hellmaster1024: we need to save the adress of tag to free the strduped mem
		  the command will retun a new adress for a new strduped string.
		  Both Strings need to be freed! */
		res_str = tag;
		player->playback->Command(player, PLAYBACK_INFO, &res_str);
		/* Hellmaster1024: in case something went wrong maybe no new adress is returned */
		if (tag != res_str)
		{
			std::string res = res_str;
			free(tag);
			free(res_str);
			return res;
		}
		else
		{
			free(tag);
			return "";
		}
	}
	free(tag);
#endif
	return "";
}

#ifdef ENABLE_MEDIAFWGSTREAMER
ePtr<iServiceInfoContainer> eServiceMP3::getInfoObject(int w)
{
	eServiceMP3InfoContainer *container = new eServiceMP3InfoContainer;
	ePtr<iServiceInfoContainer> retval = container;
	const gchar *tag = 0;
	bool isBuffer = false;
	switch (w)
	{
		case sTagTrackGain:
			tag = GST_TAG_TRACK_GAIN;
			break;
		case sTagTrackPeak:
			tag = GST_TAG_TRACK_PEAK;
			break;
		case sTagAlbumGain:
			tag = GST_TAG_ALBUM_GAIN;
			break;
		case sTagAlbumPeak:
			tag = GST_TAG_ALBUM_PEAK;
			break;
		case sTagReferenceLevel:
			tag = GST_TAG_REFERENCE_LEVEL;
			break;
		case sTagBeatsPerMinute:
			tag = GST_TAG_BEATS_PER_MINUTE;
			break;
		case sTagImage:
			tag = GST_TAG_IMAGE;
			isBuffer = true;
			break;
		case sTagPreviewImage:
			tag = GST_TAG_PREVIEW_IMAGE;
			isBuffer = true;
			break;
		case sTagAttachment:
			tag = GST_TAG_ATTACHMENT;
			isBuffer = true;
			break;
		default:
			break;
	}

	if (m_stream_tags && tag)
	{
		if (isBuffer)
		{
			const GValue *gv_buffer = gst_tag_list_get_value_index(m_stream_tags, tag, 0);
			if ( gv_buffer )
			{
				GstBuffer *buffer;
				buffer = gst_value_get_buffer (gv_buffer);
				container->setBuffer(buffer);
			}
		}
		else
		{
			gdouble value = 0.0;
			gst_tag_list_get_double(m_stream_tags, tag, &value);
			container->setDouble(value);
		}
	}
	return retval;
}
#endif

RESULT eServiceMP3::audioChannel(ePtr<iAudioChannelSelection> &ptr)
{
	ptr = this;
	return 0;
}

RESULT eServiceMP3::audioTracks(ePtr<iAudioTrackSelection> &ptr)
{
	ptr = this;
	return 0;
}

RESULT eServiceMP3::cueSheet(ePtr<iCueSheet> &ptr)
{
	ptr = this;
	return 0;
}

RESULT eServiceMP3::subtitle(ePtr<iSubtitleOutput> &ptr)
{
	ptr = this;
	return 0;
}

RESULT eServiceMP3::audioDelay(ePtr<iAudioDelay> &ptr)
{
	ptr = this;
	return 0;
}

int eServiceMP3::getNumberOfTracks()
{
 	return m_audioStreams.size();
}

int eServiceMP3::getCurrentTrack()
{
#ifdef ENABLE_MEDIAFWGSTREAMER
	if (m_currentAudioStream == -1)
		g_object_get (G_OBJECT (m_gst_playbin), "current-audio", &m_currentAudioStream, NULL);
#endif
	return m_currentAudioStream;
}

RESULT eServiceMP3::selectTrack(unsigned int i)
{
#ifdef ENABLE_MEDIAFWGSTREAMER
	bool validposition = false;
	pts_t ppos = 0;
	if (getPlayPosition(ppos) >= 0)
	{
		validposition = true;
		ppos -= 90000;
		if (ppos < 0)
			ppos = 0;
	}
	if (validposition)
	{
		/* flush */
		seekTo(ppos);
	}
	return selectAudioStream(i);
#else
	int ret = selectAudioStream(i);
	
	return ret;
#endif
}

int eServiceMP3::selectAudioStream(int i)
{
#ifdef ENABLE_MEDIAFWGSTREAMER
	int current_audio;
	g_object_set (G_OBJECT (m_gst_playbin), "current-audio", i, NULL);
#if HAVE_AMLOGIC
	if (m_currentAudioStream != i)
		AmlSwitchAudio(i);
#endif
	g_object_get (G_OBJECT (m_gst_playbin), "current-audio", &current_audio, NULL);
	if ( current_audio == i )
	{
		eDebug ("[eServiceMP3] switched to audio stream %i", current_audio);
		m_currentAudioStream = i;
		return 0;
	}
	return -1;
#else
	if (i != m_currentAudioStream)
	{
		if (player && player->playback)
			player->playback->Command(player, PLAYBACK_SWITCH_AUDIO, (void*)&i);
		m_currentAudioStream = i;
		return 0;
	}
	return -1;
#endif
}

int eServiceMP3::getCurrentChannel()
{
	return STEREO;
}

RESULT eServiceMP3::selectChannel(int i)
{
	eDebug("[eServiceMP3] selectChannel(%i)",i);
	return 0;
}

RESULT eServiceMP3::getTrackInfo(struct iAudioTrackInfo &info, unsigned int i)
{
	if (i >= m_audioStreams.size())
	{
		return -2;
#ifdef ENABLE_MEDIAFWGSTREAMER
		info.m_description = m_audioStreams[i].codec;
/*	if (m_audioStreams[i].type == atMPEG)
		info.m_description = "MPEG";
	else if (m_audioStreams[i].type == atMP3)
		info.m_description = "MP3";
	else if (m_audioStreams[i].type == atAC3)
		info.m_description = "AC3";
	else if (m_audioStreams[i].type == atAAC)
		info.m_description = "AAC";
	else if (m_audioStreams[i].type == atDTS)
		info.m_description = "DTS";
	else if (m_audioStreams[i].type == atPCM)
		info.m_description = "PCM";
	else if (m_audioStreams[i].type == atOGG)
		info.m_description = "OGG";
	else if (m_audioStreams[i].type == atFLAC)
		info.m_description = "FLAC";
	else
		info.m_description = "???";*/
	}
#else
	}

	info.m_description = m_audioStreams[i].codec;		// ????
#endif

	if (info.m_language.empty())
	{
		info.m_language = m_audioStreams[i].language_code;
	}

	return 0;
}

#ifdef ENABLE_MEDIAFWGSTREAMER
subtype_t getSubtitleType(GstPad* pad, gchar *g_codec=NULL)
{
	subtype_t type = stUnknown;
#if GST_VERSION_MAJOR < 1
	GstCaps* caps = gst_pad_get_negotiated_caps(pad);
#else
	GstCaps* caps = gst_pad_get_current_caps(pad);
#endif
	if (!caps && !g_codec)
	{
		caps = gst_pad_get_allowed_caps(pad);
	}

	if (caps && !gst_caps_is_empty(caps))
	{
		GstStructure* str = gst_caps_get_structure(caps, 0);
		if (str)
		{
			const gchar *g_type = gst_structure_get_name(str);
			eDebug("[eServiceMP3] getSubtitleType::subtitle probe caps type=%s", g_type ? g_type : "(null)");
			if (g_type)
			{
#if GST_VERSION_MAJOR < 1
				if ( !strcmp(g_type, "video/x-dvd-subpicture") )
#else
				if ( !strcmp(g_type, "subpicture/x-dvd") )
#endif
					type = stVOB;
				else if ( !strcmp(g_type, "text/x-pango-markup") )
					type = stSRT;
				else if ( !strcmp(g_type, "text/plain") || !strcmp(g_type, "text/x-plain") || !strcmp(g_type, "text/x-raw") )
					type = stPlainText;
				else if ( !strcmp(g_type, "subpicture/x-pgs") )
					type = stPGS;
				else
					eDebug("[eServiceMP3] getSubtitleType::unsupported subtitle caps %s (%s)", g_type, g_codec ? g_codec : "(null)");
			}
		}
	}
	else if ( g_codec )
	{
		eDebug("[eServiceMP3] getSubtitleType::subtitle probe codec tag=%s", g_codec);
		if ( !strcmp(g_codec, "VOB") )
			type = stVOB;
		else if ( !strcmp(g_codec, "SubStation Alpha") || !strcmp(g_codec, "SSA") )
			type = stSSA;
		else if ( !strcmp(g_codec, "ASS") )
			type = stASS;
		else if ( !strcmp(g_codec, "SRT") )
			type = stSRT;
		else if ( !strcmp(g_codec, "UTF-8 plain text") )
			type = stPlainText;
		else
			eDebug("[eServiceMP3] getSubtitleType::unsupported subtitle codec %s", g_codec);
	}
	else
		eDebug("[eServiceMP3] getSubtitleType::unidentifiable subtitle stream!");

	return type;
}

void eServiceMP3::gstBusCall(GstMessage *msg)
{
	if (!msg)
		return;
	gchar *sourceName;
	GstObject *source;
	GstElement *subsink;
	source = GST_MESSAGE_SRC(msg);
	if (!GST_IS_OBJECT(source))
		return;
	sourceName = gst_object_get_name(source);
#if 0
	gchar *string;
	if (gst_message_get_structure(msg))
		string = gst_structure_to_string(gst_message_get_structure(msg));
	else
		string = g_strdup(GST_MESSAGE_TYPE_NAME(msg));
	eDebug("[eServiceMP3] eTsRemoteSource::gst_message from %s: %s", sourceName, string);
	g_free(string);
#endif
	switch (GST_MESSAGE_TYPE (msg))
	{
		case GST_MESSAGE_EOS:
			m_event((iPlayableService*)this, evEOF);
			break;
		case GST_MESSAGE_STATE_CHANGED:
		{
			if(GST_MESSAGE_SRC(msg) != GST_OBJECT(m_gst_playbin))
				break;

			GstState old_state, new_state;
			gst_message_parse_state_changed(msg, &old_state, &new_state, NULL);

			if(old_state == new_state)
				break;

			eDebug("[eServiceMP3] state transition %s -> %s", gst_element_state_get_name(old_state), gst_element_state_get_name(new_state));

			GstStateChange transition = (GstStateChange)GST_STATE_TRANSITION(old_state, new_state);

			switch(transition)
			{
				case GST_STATE_CHANGE_NULL_TO_READY:
				{
					m_event(this, evStart);
				}	break;
				case GST_STATE_CHANGE_READY_TO_PAUSED:
				{
					m_state = stRunning;
#if GST_VERSION_MAJOR >= 1
					GValue result = { 0, };
#endif
					GstIterator *children;
					subsink = gst_bin_get_by_name(GST_BIN(m_gst_playbin), "subtitle_sink");
					if (subsink)
					{
#ifdef GSTREAMER_SUBTITLE_SYNC_MODE_BUG
						/*
						 * HACK: disable sync mode for now, gstreamer suffers from a bug causing sparse streams to loose sync, after pause/resume / skip
						 * see: https://bugzilla.gnome.org/show_bug.cgi?id=619434
						 * Sideeffect of using sync=false is that we receive subtitle buffers (far) ahead of their
						 * display time.
						 * Not too far ahead for subtitles contained in the media container.
						 * But for external srt files, we could receive all subtitles at once.
						 * And not just once, but after each pause/resume / skip.
						 * So as soon as gstreamer has been fixed to keep sync in sparse streams, sync needs to be re-enabled.
						 */
						g_object_set (G_OBJECT (subsink), "sync", FALSE, NULL);
#endif
#if 0
						/* we should not use ts-offset to sync with the decoder time, we have to do our own decoder timekeeping */
						g_object_set (G_OBJECT (subsink), "ts-offset", -2LL * GST_SECOND, NULL);
						/* late buffers probably will not occur very often */
						g_object_set (G_OBJECT (subsink), "max-lateness", 0LL, NULL);
						/* avoid prerolling (it might not be a good idea to preroll a sparse stream) */
						g_object_set (G_OBJECT (subsink), "async", TRUE, NULL);
#endif
						eDebug("[eServiceMP3] subsink properties set!");
						gst_object_unref(subsink);
					}
					if (audioSink)
					{
						gst_object_unref(GST_OBJECT(audioSink));
						audioSink = NULL;
					}
					if (videoSink)
					{
						gst_object_unref(GST_OBJECT(videoSink));
						videoSink = NULL;
					}
					children = gst_bin_iterate_recurse(GST_BIN(m_gst_playbin));
#if GST_VERSION_MAJOR < 1
					audioSink = GST_ELEMENT_CAST(gst_iterator_find_custom(children, (GCompareFunc)match_sinktype, (gpointer)"GstDVBAudioSink"));
#else
					if (gst_iterator_find_custom(children, (GCompareFunc)match_sinktype, &result, (gpointer)"GstDVBAudioSink"))
					{
						audioSink = GST_ELEMENT_CAST(g_value_dup_object(&result));
						g_value_unset(&result);
					}
#endif
					gst_iterator_free(children);
					children = gst_bin_iterate_recurse(GST_BIN(m_gst_playbin));
#if GST_VERSION_MAJOR < 1
					videoSink = GST_ELEMENT_CAST(gst_iterator_find_custom(children, (GCompareFunc)match_sinktype, (gpointer)"GstDVBVideoSink"));
#else
					if (gst_iterator_find_custom(children, (GCompareFunc)match_sinktype, &result, (gpointer)"GstDVBVideoSink"))
					{
						videoSink = GST_ELEMENT_CAST(g_value_dup_object(&result));
						g_value_unset(&result);
					}
#endif
					gst_iterator_free(children);

					/* if we are in preroll already do not check again the state */
					if (!m_is_live)
					{
						m_is_live = (gst_element_get_state(m_gst_playbin, NULL, NULL, 0LL) == GST_STATE_CHANGE_NO_PREROLL);
					}

					setAC3Delay(ac3_delay);
					setPCMDelay(pcm_delay);
					if(!m_cuesheet_loaded) /* cuesheet CVR */
						loadCuesheet();
					updateEpgCacheNowNext();

				}	break;
				case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
				{
					m_paused = false;
					if (m_currentAudioStream < 0)
					{
						int autoaudio = 0;
						int autoaudio_level = 5;
						std::string configvalue;
						std::vector<std::string> autoaudio_languages;
						configvalue = eConfigManager::getConfigValue("config.autolanguage.audio_autoselect1");
						if (configvalue != "" && configvalue != "None")
							autoaudio_languages.push_back(configvalue);
						configvalue = eConfigManager::getConfigValue("config.autolanguage.audio_autoselect2");
						if (configvalue != "" && configvalue != "None")
							autoaudio_languages.push_back(configvalue);
						configvalue = eConfigManager::getConfigValue("config.autolanguage.audio_autoselect3");
						if (configvalue != "" && configvalue != "None")
							autoaudio_languages.push_back(configvalue);
						configvalue = eConfigManager::getConfigValue("config.autolanguage.audio_autoselect4");
						if (configvalue != "" && configvalue != "None")
							autoaudio_languages.push_back(configvalue);
						for (int i = 0; i < m_audioStreams.size(); i++)
						{
							if (!m_audioStreams[i].language_code.empty())
							{
								int x = 1;
								for (std::vector<std::string>::iterator it = autoaudio_languages.begin(); x < autoaudio_level && it != autoaudio_languages.end(); x++, it++)
								{
									if ((*it).find(m_audioStreams[i].language_code) != std::string::npos)
									{
										autoaudio = i;
										autoaudio_level = x;
										break;
									}
								}
							}
						}

						if (autoaudio)
							selectTrack(autoaudio);
					}
					m_event((iPlayableService*)this, evGstreamerPlayStarted);
				}	break;
				case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
				{
					m_paused = true;
				}	break;
				case GST_STATE_CHANGE_PAUSED_TO_READY:
				{
					if (audioSink)
					{
						gst_object_unref(GST_OBJECT(audioSink));
						audioSink = NULL;
					}
					if (videoSink)
					{
						gst_object_unref(GST_OBJECT(videoSink));
						videoSink = NULL;
					}
				}	break;
				case GST_STATE_CHANGE_READY_TO_NULL:
				{
				}	break;
			}
			break;
		}
		case GST_MESSAGE_ERROR:
		{
			gchar *debug;
			GError *err;
			gst_message_parse_error (msg, &err, &debug);
			g_free (debug);
			eWarning("[eServiceMP3] Gstreamer error: %s (%i) from %s", err->message, err->code, sourceName );
			if ( err->domain == GST_STREAM_ERROR )
			{
				if ( err->code == GST_STREAM_ERROR_CODEC_NOT_FOUND )
				{
					if ( g_strrstr(sourceName, "videosink") )
						m_event((iPlayableService*)this, evUser+11);
					else if ( g_strrstr(sourceName, "audiosink") )
						m_event((iPlayableService*)this, evUser+10);
				}
			}
			else if ( err->domain == GST_RESOURCE_ERROR )
			{
				if ( err->code == GST_RESOURCE_ERROR_OPEN_READ || err->code == GST_RESOURCE_ERROR_READ )
				{
					stop();
				}
			}
			g_error_free(err);
			break;
		}
#if GST_VERSION_MAJOR >= 1
		case GST_MESSAGE_WARNING:
		{
			gchar *debug_warn = NULL;
			GError *warn = NULL;
			gst_message_parse_warning (msg, &warn, &debug_warn);
			/* CVR this Warning occurs from time to time with external srt files
			When a new seek is done the problem off to long wait times before subtitles appears,
			after movie was restarted with a resume position is solved. */
			if(!strncmp(warn->message , "Internal data flow problem", 26) && !strncmp(sourceName, "subtitle_sink", 13))
			{
				eWarning("[eServiceMP3] Gstreamer warning : %s (%i) from %s" , warn->message, warn->code, sourceName);
				subsink = gst_bin_get_by_name(GST_BIN(m_gst_playbin), "subtitle_sink");
				if(subsink)
				{
					if (!gst_element_seek (subsink, m_currentTrickRatio, GST_FORMAT_TIME, (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
						GST_SEEK_TYPE_SET, m_last_seek_pos,
						GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE))
					{
						eDebug("[eServiceMP3] seekToImpl subsink failed");
					}
					gst_object_unref(subsink);
				}
			}
			g_free(debug_warn);
			g_error_free(warn);
			break;
		}
#endif
		case GST_MESSAGE_INFO:
		{
			gchar *debug;
			GError *inf;

			gst_message_parse_info (msg, &inf, &debug);
			g_free (debug);
			if ( inf->domain == GST_STREAM_ERROR && inf->code == GST_STREAM_ERROR_DECODE )
			{
				if ( g_strrstr(sourceName, "videosink") )
					m_event((iPlayableService*)this, evUser+14);
			}
			g_error_free(inf);
			break;
		}
		case GST_MESSAGE_TAG:
		{
			GstTagList *tags, *result;
			gst_message_parse_tag(msg, &tags);

			result = gst_tag_list_merge(m_stream_tags, tags, GST_TAG_MERGE_REPLACE);
			if (result)
			{
				if (m_stream_tags && gst_tag_list_is_equal(m_stream_tags, result))
				{
					gst_tag_list_free(tags);
					gst_tag_list_free(result);
					break;
				}
				if (m_stream_tags)
					gst_tag_list_free(m_stream_tags);
				m_stream_tags = result;
			}

			const GValue *gv_image = gst_tag_list_get_value_index(tags, GST_TAG_IMAGE, 0);
			if ( gv_image )
			{
				GstBuffer *buf_image;
#if GST_VERSION_MAJOR < 1
				buf_image = gst_value_get_buffer(gv_image);
#else
				GstSample *sample;
				sample = (GstSample *)g_value_get_boxed(gv_image);
				buf_image = gst_sample_get_buffer(sample);
#endif
				int fd = open("/tmp/.id3coverart", O_CREAT|O_WRONLY|O_TRUNC, 0644);
				if (fd >= 0)
				{
					guint8 *data;
					gsize size;
#if GST_VERSION_MAJOR < 1
					data = GST_BUFFER_DATA(buf_image);
					size = GST_BUFFER_SIZE(buf_image);
#else
					GstMapInfo map;
					gst_buffer_map(buf_image, &map, GST_MAP_READ);
					data = map.data;
					size = map.size;
#endif
					int ret = write(fd, data, size);
#if GST_VERSION_MAJOR >= 1
					gst_buffer_unmap(buf_image, &map);
#endif
					close(fd);
					eDebug("[eServiceMP3] /tmp/.id3coverart %d bytes written ", ret);
				}
				m_event((iPlayableService*)this, evUser+13);
			}
			gst_tag_list_free(tags);
			m_event((iPlayableService*)this, evUpdatedInfo);
			break;
		}
		/* TOC entry intercept used for chapter support CVR */
#if GST_VERSION_MAJOR >= 1
		case GST_MESSAGE_TOC:
		{
			HandleTocEntry(msg);
			break;
		}
#endif
		case GST_MESSAGE_ASYNC_DONE:
		{
			if(GST_MESSAGE_SRC(msg) != GST_OBJECT(m_gst_playbin))
				break;

			gint i, n_video = 0, n_audio = 0, n_text = 0;

			g_object_get (m_gst_playbin, "n-video", &n_video, NULL);
			g_object_get (m_gst_playbin, "n-audio", &n_audio, NULL);
			g_object_get (m_gst_playbin, "n-text", &n_text, NULL);


			eDebug("[eServiceMP3] async-done - %d video, %d audio, %d subtitle", n_video, n_audio, n_text);

			if ( n_video + n_audio <= 0 )
				stop();

			m_audioStreams.clear();
			m_subtitleStreams.clear();

			for (i = 0; i < n_audio; i++)
			{
				audioStream audio;
				gchar *g_codec, *g_lang;
				GstTagList *tags = NULL;
				GstPad* pad = 0;
				g_signal_emit_by_name (m_gst_playbin, "get-audio-pad", i, &pad);
#if GST_VERSION_MAJOR < 1
				GstCaps* caps = gst_pad_get_negotiated_caps(pad);
#else
				GstCaps* caps = gst_pad_get_current_caps(pad);
#endif
				gst_object_unref(pad);
				if (!caps)
					continue;
				GstStructure* str = gst_caps_get_structure(caps, 0);
				const gchar *g_type = gst_structure_get_name(str);
				eDebug("[eServiceMP3] AUDIO STRUCT=%s", g_type);
				audio.type = gstCheckAudioPad(str);
				audio.language_code = "und";
				audio.codec = g_type;
				g_codec = NULL;
				g_lang = NULL;
				g_signal_emit_by_name (m_gst_playbin, "get-audio-tags", i, &tags);
#if GST_VERSION_MAJOR < 1
				if (tags && gst_is_tag_list(tags))
#else
				if (tags && GST_IS_TAG_LIST(tags))
#endif
				{
					if (gst_tag_list_get_string(tags, GST_TAG_AUDIO_CODEC, &g_codec))
					{
						audio.codec = std::string(g_codec);
						g_free(g_codec);
					}
					if (gst_tag_list_get_string(tags, GST_TAG_LANGUAGE_CODE, &g_lang))
					{
						audio.language_code = std::string(g_lang);
						g_free(g_lang);
					}
					gst_tag_list_free(tags);
				}
				eDebug("[eServiceMP3] audio stream=%i codec=%s language=%s", i, audio.codec.c_str(), audio.language_code.c_str());
				m_audioStreams.push_back(audio);
				gst_caps_unref(caps);
			}

			for (i = 0; i < n_text; i++)
			{
				gchar *g_codec = NULL, *g_lang = NULL;
				GstTagList *tags = NULL;
				g_signal_emit_by_name (m_gst_playbin, "get-text-tags", i, &tags);
				subtitleStream subs;
				subs.language_code = "und";
#if GST_VERSION_MAJOR < 1
				if (tags && gst_is_tag_list(tags))
#else
				if (tags && GST_IS_TAG_LIST(tags))
#endif
				{
					if (gst_tag_list_get_string(tags, GST_TAG_LANGUAGE_CODE, &g_lang))
					{
						subs.language_code = g_lang;
						g_free(g_lang);
					}
					gst_tag_list_get_string(tags, GST_TAG_SUBTITLE_CODEC, &g_codec);
					gst_tag_list_free(tags);
				}

				eDebug("[eServiceMP3] subtitle stream=%i language=%s codec=%s", i, subs.language_code.c_str(), g_codec ? g_codec : "(null)");

				GstPad* pad = 0;
				g_signal_emit_by_name (m_gst_playbin, "get-text-pad", i, &pad);
				if ( pad )
					g_signal_connect (G_OBJECT (pad), "notify::caps", G_CALLBACK (gstTextpadHasCAPS), this);

				subs.type = getSubtitleType(pad, g_codec);
				gst_object_unref(pad);
				g_free(g_codec);
				m_subtitleStreams.push_back(subs);
			}

			m_event((iPlayableService*)this, evUpdatedInfo);
			if (m_seek_paused)
			{
				m_seek_paused = false;
				gst_element_set_state(m_gst_playbin, GST_STATE_PAUSED);
			}

			if ( m_errorInfo.missing_codec != "" )
			{
				if (m_errorInfo.missing_codec.find("video/") == 0 || (m_errorInfo.missing_codec.find("audio/") == 0 && m_audioStreams.empty()))
					m_event((iPlayableService*)this, evUser+12);
			}
			break;
		}
		case GST_MESSAGE_ELEMENT:
		{
			const GstStructure *msgstruct = gst_message_get_structure(msg);
			if (msgstruct)
			{
				if ( gst_is_missing_plugin_message(msg) )
				{
					GstCaps *caps = NULL;
					gst_structure_get (msgstruct, "detail", GST_TYPE_CAPS, &caps, NULL);
					if (caps)
					{
						std::string codec = (const char*) gst_caps_to_string(caps);
						gchar *description = gst_missing_plugin_message_get_description(msg);
						if ( description )
						{
							eDebug("[eServiceMP3] m_errorInfo.missing_codec = %s", codec.c_str());
							m_errorInfo.error_message = "GStreamer plugin " + (std::string)description + " not available!\n";
							m_errorInfo.missing_codec = codec.substr(0,(codec.find_first_of(',')));
							g_free(description);
						}
						gst_caps_unref(caps);
					}
				}
				else
				{
					const gchar *eventname = gst_structure_get_name(msgstruct);
					if ( eventname )
					{
						if (!strcmp(eventname, "eventSizeChanged") || !strcmp(eventname, "eventSizeAvail"))
						{
							gst_structure_get_int (msgstruct, "aspect_ratio", &m_aspect);
							gst_structure_get_int (msgstruct, "width", &m_width);
							gst_structure_get_int (msgstruct, "height", &m_height);
							if (strstr(eventname, "Changed"))
								m_event((iPlayableService*)this, evVideoSizeChanged);
						}
						else if (!strcmp(eventname, "eventFrameRateChanged") || !strcmp(eventname, "eventFrameRateAvail"))
						{
							gst_structure_get_int (msgstruct, "frame_rate", &m_framerate);
							if (strstr(eventname, "Changed"))
								m_event((iPlayableService*)this, evVideoFramerateChanged);
						}
						else if (!strcmp(eventname, "eventProgressiveChanged") || !strcmp(eventname, "eventProgressiveAvail"))
						{
							gst_structure_get_int (msgstruct, "progressive", &m_progressive);
							if (strstr(eventname, "Changed"))
								m_event((iPlayableService*)this, evVideoProgressiveChanged);
						}
						else if (!strcmp(eventname, "redirect"))
						{
							const char *uri = gst_structure_get_string(msgstruct, "new-location");
							eDebug("[eServiceMP3] redirect to %s", uri);
							gst_element_set_state (m_gst_playbin, GST_STATE_NULL);
							g_object_set(G_OBJECT (m_gst_playbin), "uri", uri, NULL);
							gst_element_set_state (m_gst_playbin, GST_STATE_PLAYING);
						}
					}
				}
			}
			break;
		}
		case GST_MESSAGE_BUFFERING:
			if (m_sourceinfo.is_streaming)
			{
				GstBufferingMode mode;
				gst_message_parse_buffering(msg, &(m_bufferInfo.bufferPercent));
				eLog(6, "[eServiceMP3] Buffering %u percent done", m_bufferInfo.bufferPercent);
				gst_message_parse_buffering_stats(msg, &mode, &(m_bufferInfo.avgInRate), &(m_bufferInfo.avgOutRate), &(m_bufferInfo.bufferingLeft));
				m_event((iPlayableService*)this, evBuffering);
				/*
				 * we don't react to buffer level messages, unless we are configured to use a prefill buffer
				 * (even if we are not configured to, we still use the buffer, but we rely on it to remain at the
				 * healthy level at all times, without ever having to pause the stream)
				 *
				 * Also, it does not make sense to pause the stream if it is a live stream
				 * (in which case the sink will not produce data while paused, so we won't
				 * recover from an empty buffer)
				 */
				if (m_use_prefillbuffer && !m_is_live && --m_ignore_buffering_messages <= 0)
				{
					if (m_bufferInfo.bufferPercent == 100)
					{
						GstState state;
						gst_element_get_state(m_gst_playbin, &state, NULL, 0LL);
						if (state != GST_STATE_PLAYING)
						{
							eDebug("[eServiceMP3] start playing");
							gst_element_set_state (m_gst_playbin, GST_STATE_PLAYING);
						}
						/*
						 * when we start the pipeline, the contents of the buffer will immediately drain
						 * into the (hardware buffers of the) sinks, so we will receive low buffer level
						 * messages right away.
						 * Ignore the first few buffering messages, giving the buffer the chance to recover
						 * a bit, before we start handling empty buffer states again.
						 */
						m_ignore_buffering_messages = 5;
					}
					else if (m_bufferInfo.bufferPercent == 0)
					{
						eDebug("[eServiceMP3] start pause");
						gst_element_set_state (m_gst_playbin, GST_STATE_PAUSED);
						m_ignore_buffering_messages = 0;
					}
					else
					{
						m_ignore_buffering_messages = 0;
					}
				}
			}
			break;
		default:
			break;
	}
	g_free (sourceName);
}

void eServiceMP3::handleMessage(GstMessage *msg)
{
	if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_STATE_CHANGED && GST_MESSAGE_SRC(msg) != GST_OBJECT(m_gst_playbin))
	{
		/*
		 * ignore verbose state change messages for all active elements;
		 * we only need to handle state-change events for the playbin
		 */
		gst_message_unref(msg);
		return;
	}
	m_pump.send(new GstMessageContainer(1, msg, NULL, NULL));
}

GstBusSyncReply eServiceMP3::gstBusSyncHandler(GstBus *bus, GstMessage *message, gpointer user_data)
{
	eServiceMP3 *_this = (eServiceMP3*)user_data;
	if (_this) _this->handleMessage(message);
	return GST_BUS_DROP;
}
/*Processing TOC CVR */
#if GST_VERSION_MAJOR >= 1
void eServiceMP3::HandleTocEntry(GstMessage *msg)
{
	/* limit TOC to dvbvideosink cue sheet only works for video media */
	if (!strncmp(GST_MESSAGE_SRC_NAME(msg), "dvbvideosink", 12))
	{
		GstToc *toc;
		gboolean updated;
		gst_message_parse_toc(msg, &toc, &updated);
		for (GList* i = gst_toc_get_entries(toc); i; i = i->next)
		{
			GstTocEntry *entry = static_cast<GstTocEntry*>(i->data);
			if (gst_toc_entry_get_entry_type (entry) == GST_TOC_ENTRY_TYPE_EDITION)
			{
				/* extra debug info for testing purposes CVR should_be_removed later on */
				eLog(5, "[eServiceMP3] toc_type %s", gst_toc_entry_type_get_nick(gst_toc_entry_get_entry_type (entry)));
				gint y = 0;
				for (GList* x = gst_toc_entry_get_sub_entries (entry); x; x = x->next)
				{
					GstTocEntry *sub_entry = static_cast<GstTocEntry*>(x->data);
					if (gst_toc_entry_get_entry_type (sub_entry) == GST_TOC_ENTRY_TYPE_CHAPTER)
					{
						if (y == 0)
						{
							m_use_chapter_entries = true;
							if (m_cuesheet_loaded)
								m_cue_entries.clear();
							else
								loadCuesheet();
						}
						/* first chapter is movie start no cut needed */
						else if (y >= 1)
						{
							gint64 start = 0;
							gint64 pts = 0;
							gint type = 0;
							gst_toc_entry_get_start_stop_times(sub_entry, &start, NULL);
							type = 2;
							if(start > 0)
								pts = start / 11111;
							if (pts > 0)
							{
								m_cue_entries.insert(cueEntry(pts, type));
								//m_cuesheet_changed = 1;
								//m_event((iPlayableService*)this, evCuesheetChanged);
								/* extra debug info for testing purposes CVR should_be_removed later on */
								eLog(5, "[eServiceMP3] toc_subtype %s,Nr = %d, start= %#"G_GINT64_MODIFIER "x",
										gst_toc_entry_type_get_nick(gst_toc_entry_get_entry_type (sub_entry)), y + 1, pts);
							}
						}
						y++;
					}
				}
				if (y > 0)
				{
					m_cuesheet_changed = 1;
					m_event((iPlayableService*)this, evCuesheetChanged);
				}
			}
		}
		eDebug("[eServiceMP3] TOC entry from source %s processed", GST_MESSAGE_SRC_NAME(msg));
	}
	else
	{
		eDebug("[eServiceMP3] TOC entry from source %s not used", GST_MESSAGE_SRC_NAME(msg));
	}
}
#endif
void eServiceMP3::playbinNotifySource(GObject *object, GParamSpec *unused, gpointer user_data)
{
	GstElement *source = NULL;
	eServiceMP3 *_this = (eServiceMP3*)user_data;
	g_object_get(object, "source", &source, NULL);
	if (source)
	{
		if (g_object_class_find_property(G_OBJECT_GET_CLASS(source), "timeout") != 0)
		{
			GstElementFactory *factory = gst_element_get_factory(source);
			if (factory)
			{
				const gchar *sourcename = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));
				if (!strcmp(sourcename, "souphttpsrc"))
				{
					g_object_set(G_OBJECT(source), "timeout", HTTP_TIMEOUT, NULL);
				}
			}
		}
		if (g_object_class_find_property(G_OBJECT_GET_CLASS(source), "ssl-strict") != 0)
		{
			g_object_set(G_OBJECT(source), "ssl-strict", FALSE, NULL);
		}
		if (g_object_class_find_property(G_OBJECT_GET_CLASS(source), "user-agent") != 0 && !_this->m_useragent.empty())
		{
			g_object_set(G_OBJECT(source), "user-agent", _this->m_useragent.c_str(), NULL);
		}
		if (g_object_class_find_property(G_OBJECT_GET_CLASS(source), "extra-headers") != 0 && !_this->m_extra_headers.empty())
		{
#if GST_VERSION_MAJOR < 1
			GstStructure *extras = gst_structure_empty_new("extras");
#else
			GstStructure *extras = gst_structure_new_empty("extras");
#endif
			size_t pos = 0;
			while (pos != std::string::npos)
			{
				std::string name, value;
				size_t start = pos;
				size_t len = std::string::npos;
				pos = _this->m_extra_headers.find('=', pos);
				if (pos != std::string::npos)
				{
					len = pos - start;
					pos++;
					name = _this->m_extra_headers.substr(start, len);
					start = pos;
					len = std::string::npos;
					pos = _this->m_extra_headers.find('&', pos);
					if (pos != std::string::npos)
					{
						len = pos - start;
						pos++;
					}
					value = _this->m_extra_headers.substr(start, len);
				}
				if (!name.empty() && !value.empty())
				{
					GValue header;
					eDebug("[eServiceMP3] setting extra-header '%s:%s'", name.c_str(), value.c_str());
					memset(&header, 0, sizeof(GValue));
					g_value_init(&header, G_TYPE_STRING);
					g_value_set_string(&header, value.c_str());
					gst_structure_set_value(extras, name.c_str(), &header);
				}
				else
				{
					eDebug("[eServiceMP3] Invalid header format %s", _this->m_extra_headers.c_str());
					break;
				}
			}
			if (gst_structure_n_fields(extras) > 0)
			{
				g_object_set(G_OBJECT(source), "extra-headers", extras, NULL);
			}
			gst_structure_free(extras);
		}
		gst_object_unref(source);
	}
}

void eServiceMP3::handleElementAdded(GstBin *bin, GstElement *element, gpointer user_data)
{
	eServiceMP3 *_this = (eServiceMP3*)user_data;
	if (_this)
	{
		gchar *elementname = gst_element_get_name(element);

		if (g_str_has_prefix(elementname, "queue2"))
		{
			if (_this->m_download_buffer_path != "")
			{
				g_object_set(G_OBJECT(element), "temp-template", _this->m_download_buffer_path.c_str(), NULL);
			}
			else
			{
				g_object_set(G_OBJECT(element), "temp-template", NULL, NULL);
			}
		}
		else if (g_str_has_prefix(elementname, "uridecodebin")
#if GST_VERSION_MAJOR < 1
			|| g_str_has_prefix(elementname, "decodebin2"))
#else
			|| g_str_has_prefix(elementname, "decodebin"))
#endif
		{
			/*
			 * Listen for queue2 element added to uridecodebin/decodebin2 as well.
			 * Ignore other bins since they may have unrelated queues
			 */
				g_signal_connect(element, "element-added", G_CALLBACK(handleElementAdded), user_data);
		}
		g_free(elementname);
	}
}

audiotype_t eServiceMP3::gstCheckAudioPad(GstStructure* structure)
{
	if (!structure)
		return atUnknown;

	if ( gst_structure_has_name (structure, "audio/mpeg"))
	{
		gint mpegversion, layer = -1;
		if (!gst_structure_get_int (structure, "mpegversion", &mpegversion))
			return atUnknown;

		switch (mpegversion) {
			case 1:
				{
					gst_structure_get_int (structure, "layer", &layer);
					if ( layer == 3 )
						return atMP3;
					else
						return atMPEG;
					break;
				}
			case 2:
				return atAAC;
			case 4:
				return atAAC;
			default:
				return atUnknown;
		}
	}

	else if ( gst_structure_has_name (structure, "audio/x-ac3") || gst_structure_has_name (structure, "audio/ac3") )
		return atAC3;
	else if ( gst_structure_has_name (structure, "audio/x-dts") || gst_structure_has_name (structure, "audio/dts") )
		return atDTS;
#if GST_VERSION_MAJOR < 1
	else if ( gst_structure_has_name (structure, "audio/x-raw-int") )
#else
	else if ( gst_structure_has_name (structure, "audio/x-raw") )
#endif
		return atPCM;

	return atUnknown;
}

void eServiceMP3::gstPoll(ePtr<GstMessageContainer> const &msg)
{
	switch (msg->getType())
	{
		case 1:
		{
			GstMessage *gstmessage = *((GstMessageContainer*)msg);
			if (gstmessage)
			{
				gstBusCall(gstmessage);
			}
			break;
		}
		case 2:
		{
			GstBuffer *buffer = *((GstMessageContainer*)msg);
			if (buffer)
			{
				pullSubtitle(buffer);
			}
			break;
		}
		case 3:
		{
			GstPad *pad = *((GstMessageContainer*)msg);
			gstTextpadHasCAPS_synced(pad);
			break;
		}
	}
}
#endif

eAutoInitPtr<eServiceFactoryMP3> init_eServiceFactoryMP3(eAutoInitNumbers::service+1, "eServiceFactoryMP3");

#ifdef ENABLE_MEDIAFWGSTREAMER
void eServiceMP3::gstCBsubtitleAvail(GstElement *subsink, GstBuffer *buffer, gpointer user_data)
{
	eServiceMP3 *_this = (eServiceMP3*)user_data;
	if (_this->m_currentSubtitleStream < 0)
	{
		if (buffer) gst_buffer_unref(buffer);
		return;
	}
	_this->m_pump.send(new GstMessageContainer(2, NULL, NULL, buffer));
}

void eServiceMP3::gstTextpadHasCAPS(GstPad *pad, GParamSpec * unused, gpointer user_data)
{
	eServiceMP3 *_this = (eServiceMP3*)user_data;

	gst_object_ref (pad);

	_this->m_pump.send(new GstMessageContainer(3, NULL, pad, NULL));
}

void eServiceMP3::gstTextpadHasCAPS_synced(GstPad *pad)
{
	GstCaps *caps = NULL;

	g_object_get (G_OBJECT (pad), "caps", &caps, NULL);

	if (caps)
	{
		subtitleStream subs;

		eDebug("[eServiceMP3] gstTextpadHasCAPS:: signal::caps = %s", gst_caps_to_string(caps));
//		eDebug("[eServiceMP3] gstGhostpadHasCAPS_synced %p %d", pad, m_subtitleStreams.size());

		if (m_currentSubtitleStream >= 0 && m_currentSubtitleStream < (int)m_subtitleStreams.size())
			subs = m_subtitleStreams[m_currentSubtitleStream];
		else {
			subs.type = stUnknown;
			subs.pad = pad;
		}

		if ( subs.type == stUnknown )
		{
			GstTagList *tags = NULL;
			gchar *g_lang = NULL;
			g_signal_emit_by_name (m_gst_playbin, "get-text-tags", m_currentSubtitleStream, &tags);

			subs.language_code = "und";
			subs.type = getSubtitleType(pad);
#if GST_VERSION_MAJOR < 1
			if (tags && gst_is_tag_list(tags))
#else
			if (tags && GST_IS_TAG_LIST(tags))
#endif
			{
				if (gst_tag_list_get_string(tags, GST_TAG_LANGUAGE_CODE, &g_lang))
				{
					subs.language_code = std::string(g_lang);
					g_free(g_lang);
				}
				gst_tag_list_free(tags);
			}

			if (m_currentSubtitleStream >= 0 && m_currentSubtitleStream < (int)m_subtitleStreams.size())
				m_subtitleStreams[m_currentSubtitleStream] = subs;
			else
				m_subtitleStreams.push_back(subs);
		}

//		eDebug("[eServiceMP3] gstGhostpadHasCAPS:: m_gst_prev_subtitle_caps=%s equal=%i",gst_caps_to_string(m_gst_prev_subtitle_caps),gst_caps_is_equal(m_gst_prev_subtitle_caps, caps));

		gst_caps_unref (caps);
	}
}

void eServiceMP3::pullSubtitle(GstBuffer *buffer)
{
	if (buffer && m_currentSubtitleStream >= 0 && m_currentSubtitleStream < (int)m_subtitleStreams.size())
	{
#if GST_VERSION_MAJOR < 1
		gint64 buf_pos = GST_BUFFER_TIMESTAMP(buffer);
		size_t len = GST_BUFFER_SIZE(buffer);
#else
		GstMapInfo map;
		if(!gst_buffer_map(buffer, &map, GST_MAP_READ))
		{
			eLog(3, "[eServiceMP3] pullSubtitle gst_buffer_map failed");
			return;
		}
		gint64 buf_pos = GST_BUFFER_PTS(buffer);
		size_t len = map.size;
		eLog(6, "[eServiceMP3] gst_buffer_get_size %zu map.size %zu", gst_buffer_get_size(buffer), len);
#endif
		gint64 duration_ns = GST_BUFFER_DURATION(buffer);
		int subType = m_subtitleStreams[m_currentSubtitleStream].type;
		eLog(6, "[eServiceMP3] pullSubtitle type=%d size=%zu", subType, len);
		if ( subType )
		{
			if ( subType < stVOB )
			{
				int delay = eConfigManager::getConfigIntValue("config.subtitles.pango_subtitles_delay");
				int subtitle_fps = eConfigManager::getConfigIntValue("config.subtitles.pango_subtitles_fps");

				double convert_fps = 1.0;
				if (subtitle_fps > 1 && m_framerate > 0)
					convert_fps = subtitle_fps / (double)m_framerate;

#if GST_VERSION_MAJOR < 1
				std::string line((const char*)GST_BUFFER_DATA(buffer), len);
#else
				std::string line((const char*)map.data, len);
#endif
				// some media muxers do add an extra new line at the end off a muxed/reencoded srt to ssa codec
				if (!line.empty() && line[line.length()-1] == '\n')
					line.erase(line.length()-1);

				eLog(6, "[eServiceMP3] got new text subtitle @ buf_pos = %lld ns (in pts=%lld), dur=%lld: '%s' ", buf_pos, buf_pos/11111, duration_ns, line.c_str());

				uint32_t start_ms = ((buf_pos / 1000000ULL) * convert_fps) + (delay / 90);
				uint32_t end_ms = start_ms + (duration_ns / 1000000ULL);
				m_subtitle_pages.insert(subtitle_pages_map_pair_t(end_ms, subtitle_page_t(start_ms, end_ms, line)));
				m_subtitle_sync_timer->start(1, true);
			}
			else
			{
				eLog(3, "[eServiceMP3] unsupported subpicture... ignoring");
			}
		}
#if GST_VERSION_MAJOR >= 1
		gst_buffer_unmap(buffer, &map);
#endif
	}
}
#else
void eServiceMP3::eplayerCBsubtitleAvail(long int duration_ms, size_t len, char * buffer, void* user_data)
{
	eDebug("eServiceMP3::%s >", __func__);
	unsigned char tmp[len+1];
	memcpy(tmp, buffer, len);
	tmp[len] = 0;
	eDebug("gstCBsubtitleAvail: %s", tmp);
	eServiceMP3 *_this = (eServiceMP3*)user_data;
	if ( _this->m_subtitle_widget )
	{
		ePangoSubtitlePage page;
		gRGB rgbcol(0xD0,0xD0,0xD0);
		page.m_elements.push_back(ePangoSubtitlePageElement(rgbcol, (const char*)tmp));
		page.m_timeout = duration_ms;
		(_this->m_subtitle_widget)->setPage(page);
	}
	eDebug("eServiceMP3::%s <", __func__);
}
#endif

void eServiceMP3::pushSubtitles()
{
#ifdef ENABLE_MEDIAFWGSTREAMER
	pts_t running_pts = 0;
	int32_t next_timer = 0, decoder_ms, start_ms, end_ms, diff_start_ms, diff_end_ms;
	subtitle_pages_map_t::iterator current;

	// wait until clock is stable

	if (getPlayPosition(running_pts) < 0)
		m_decoder_time_valid_state = 0;

	if (m_decoder_time_valid_state < 4)
	{
		m_decoder_time_valid_state++;

		if (m_prev_decoder_time == running_pts)
			m_decoder_time_valid_state = 0;

		if (m_decoder_time_valid_state < 4)
		{
			//eDebug("[eServiceMP3] *** push subtitles, waiting for clock to stabilise");
			m_prev_decoder_time = running_pts;
			next_timer = 50;
			goto exit;
		}

		//eDebug("[eServiceMP3] *** push subtitles, clock stable");
	}

	decoder_ms = running_pts / 90;

#if 0
		eDebug("[eServiceMP3] *** all subs: ");

		for (current = m_subtitle_pages.begin(); current != m_subtitle_pages.end(); current++)
		{
			start_ms = current->second.start_ms;
			end_ms = current->second.end_ms;
			diff_start_ms = start_ms - decoder_ms;
			diff_end_ms = end_ms - decoder_ms;

			eDebug("[eServiceMP3]    start: %d, end: %d, diff_start: %d, diff_end: %d: %s",
					start_ms, end_ms, diff_start_ms, diff_end_ms, current->second.text.c_str());
		}

#endif

	for (current = m_subtitle_pages.lower_bound(decoder_ms); current != m_subtitle_pages.end(); current++)
	{
		start_ms = current->second.start_ms;
		end_ms = current->second.end_ms;
		diff_start_ms = start_ms - decoder_ms;
		diff_end_ms = end_ms - decoder_ms;

#if 0
		eDebug("[eServiceMP3] *** next subtitle: decoder: %d, start: %d, end: %d, duration_ms: %d, diff_start: %d, diff_end: %d : %s",
			decoder_ms, start_ms, end_ms, end_ms - start_ms, diff_start_ms, diff_end_ms, current->second.text.c_str());
#endif

		if (diff_end_ms < 0)
		{
			//eDebug("[eServiceMP3] *** current sub has already ended, skip: %d", diff_end_ms);
			continue;
		}

		if (diff_start_ms > 20)
		{
			//eDebug("[eServiceMP3] *** current sub in the future, start timer, %d", diff_start_ms);
			next_timer = diff_start_ms;
			goto exit;
		}

		// showtime

		if (m_subtitle_widget && !m_paused)
		{
			//eDebug("[eServiceMP3] *** current sub actual, show!");

			ePangoSubtitlePage pango_page;
			gRGB rgbcol(0xD0,0xD0,0xD0);

			pango_page.m_elements.push_back(ePangoSubtitlePageElement(rgbcol, current->second.text.c_str()));
			pango_page.m_show_pts = start_ms * 90;			// actually completely unused by widget!
			pango_page.m_timeout = end_ms - decoder_ms;		// take late start into account

			m_subtitle_widget->setPage(pango_page);
		}

		//eDebug("[eServiceMP3] *** no next sub scheduled, check NEXT subtitle");
	}

	// no more subs in cache, fall through

exit:
	if (next_timer == 0)
	{
		//eDebug("[eServiceMP3] *** next timer = 0, set default timer!");
		next_timer = 1000;
	}

	m_subtitle_sync_timer->start(next_timer, true);

#endif
}

RESULT eServiceMP3::enableSubtitles(iSubtitleUser *user, struct SubtitleTrack &track)
{
	if (m_currentSubtitleStream != track.pid)
	{
#ifdef ENABLE_MEDIAFWGSTREAMER
		g_object_set (G_OBJECT (m_gst_playbin), "current-text", -1, NULL);
#endif
		m_subtitle_sync_timer->stop();
		m_subtitle_pages.clear();
		m_prev_decoder_time = -1;
		m_decoder_time_valid_state = 0;
#ifdef ENABLE_MEDIAFWGSTREAMER
		m_currentSubtitleStream = track.pid;
		m_cachedSubtitleStream = m_currentSubtitleStream;
		g_object_set (G_OBJECT (m_gst_playbin), "current-text", m_currentSubtitleStream, NULL);
#endif

		m_subtitle_widget = user;

#ifdef ENABLE_MEDIAFWGSTREAMER
//		eDebug ("[eServiceMP3] switched to subtitle stream %i", m_currentSubtitleStream);

#ifdef GSTREAMER_SUBTITLE_SYNC_MODE_BUG
		/*
		 * when we're running the subsink in sync=false mode,
		 * we have to force a seek, before the new subtitle stream will start
		 */
		seekRelative(-1, 90000);
#endif
#endif
	}
#ifdef ENABLE_MEDIAFWGSTREAMER
#else
	if (player && player->playback)
		player->playback->Command(player, PLAYBACK_SWITCH_SUBTITLE, (void*)&track.pid);
#endif

	return 0;
}

RESULT eServiceMP3::disableSubtitles()
{
#ifdef ENABLE_MEDIAFWGSTREAMER
	eDebug("[eServiceMP3] disableSubtitles");
	m_currentSubtitleStream = -1;
	m_cachedSubtitleStream = m_currentSubtitleStream;
	g_object_set (G_OBJECT (m_gst_playbin), "current-text", m_currentSubtitleStream, NULL);
#endif
	m_subtitle_sync_timer->stop();
	m_subtitle_pages.clear();
	m_prev_decoder_time = -1;
	m_decoder_time_valid_state = 0;
	if (m_subtitle_widget) m_subtitle_widget->destroy();
	m_subtitle_widget = 0;
#ifdef ENABLE_MEDIAFWGSTREAMER
#else
	int pid = -1;
	if (player && player->playback)
		player->playback->Command(player, PLAYBACK_SWITCH_SUBTITLE, (void*)&pid);
#endif
	return 0;
}

RESULT eServiceMP3::getCachedSubtitle(struct SubtitleTrack &track)
{

	bool autoturnon = eConfigManager::getConfigBoolValue("config.subtitles.pango_autoturnon", true);
	int m_subtitleStreams_size = (int)m_subtitleStreams.size();
	if (!autoturnon)
		return -1;

	if (m_cachedSubtitleStream == -2 && m_subtitleStreams_size)
	{
		m_cachedSubtitleStream = 0;
		int autosub_level = 5;
		std::string configvalue;
		std::vector<std::string> autosub_languages;
		configvalue = eConfigManager::getConfigValue("config.autolanguage.subtitle_autoselect1");
		if (configvalue != "" && configvalue != "None")
			autosub_languages.push_back(configvalue);
		configvalue = eConfigManager::getConfigValue("config.autolanguage.subtitle_autoselect2");
		if (configvalue != "" && configvalue != "None")
			autosub_languages.push_back(configvalue);
		configvalue = eConfigManager::getConfigValue("config.autolanguage.subtitle_autoselect3");
		if (configvalue != "" && configvalue != "None")
			autosub_languages.push_back(configvalue);
		configvalue = eConfigManager::getConfigValue("config.autolanguage.subtitle_autoselect4");
		if (configvalue != "" && configvalue != "None")
			autosub_languages.push_back(configvalue);
		for (int i = 0; i < m_subtitleStreams_size; i++)
		{
			if (!m_subtitleStreams[i].language_code.empty())
			{
				int x = 1;
				for (std::vector<std::string>::iterator it2 = autosub_languages.begin(); x < autosub_level && it2 != autosub_languages.end(); x++, it2++)
				{
					if ((*it2).find(m_subtitleStreams[i].language_code) != std::string::npos)
					{
						autosub_level = x;
						m_cachedSubtitleStream = i;
						break;
					}
				}
			}
		}
	}

	if (m_cachedSubtitleStream >= 0 && m_cachedSubtitleStream < m_subtitleStreams_size)
	{
		track.type = 2;
		track.pid = m_cachedSubtitleStream;
		track.page_number = int(m_subtitleStreams[m_cachedSubtitleStream].type);
		track.magazine_number = 0;
		return 0;
	}
	return -1;
}

RESULT eServiceMP3::getSubtitleList(std::vector<struct SubtitleTrack> &subtitlelist)
{
// 	eDebug("[eServiceMP3] getSubtitleList");
	int stream_idx = 0;

	for (std::vector<subtitleStream>::iterator IterSubtitleStream(m_subtitleStreams.begin()); IterSubtitleStream != m_subtitleStreams.end(); ++IterSubtitleStream)
	{
		subtype_t type = IterSubtitleStream->type;
		switch(type)
		{
		case stUnknown:
		case stVOB:
		case stPGS:
			break;
		default:
		{
			struct SubtitleTrack track;
			track.type = 2;
			track.pid = stream_idx;
			track.page_number = int(type);
			track.magazine_number = 0;
			track.language_code = IterSubtitleStream->language_code;
			subtitlelist.push_back(track);
		}
		}
		stream_idx++;
	}
	eDebug("[eServiceMP3] getSubtitleList finished");
	return 0;
}

RESULT eServiceMP3::streamed(ePtr<iStreamedService> &ptr)
{
	ptr = this;
	return 0;
}

ePtr<iStreamBufferInfo> eServiceMP3::getBufferCharge()
{
	return new eStreamBufferInfo(m_bufferInfo.bufferPercent, m_bufferInfo.avgInRate, m_bufferInfo.avgOutRate, m_bufferInfo.bufferingLeft, m_buffer_size);
}
/* cuesheet CVR */
PyObject *eServiceMP3::getCutList()
{
	ePyObject list = PyList_New(0);

	for (std::multiset<struct cueEntry>::iterator i(m_cue_entries.begin()); i != m_cue_entries.end(); ++i)
	{
		ePyObject tuple = PyTuple_New(2);
		PyTuple_SET_ITEM(tuple, 0, PyLong_FromLongLong(i->where));
		PyTuple_SET_ITEM(tuple, 1, PyInt_FromLong(i->what));
		PyList_Append(list, tuple);
		Py_DECREF(tuple);
	}

	return list;
}
/* cuesheet CVR */
void eServiceMP3::setCutList(ePyObject list)
{
	if (!PyList_Check(list))
		return;
	int size = PyList_Size(list);
	int i;

	m_cue_entries.clear();

	for (i=0; i<size; ++i)
	{
		ePyObject tuple = PyList_GET_ITEM(list, i);
		if (!PyTuple_Check(tuple))
		{
			eDebug("[eServiceMP3] non-tuple in cutlist");
			continue;
		}
		if (PyTuple_Size(tuple) != 2)
		{
			eDebug("[eServiceMP3] cutlist entries need to be a 2-tuple");
			continue;
		}
		ePyObject ppts = PyTuple_GET_ITEM(tuple, 0), ptype = PyTuple_GET_ITEM(tuple, 1);
		if (!(PyLong_Check(ppts) && PyInt_Check(ptype)))
		{
			eDebug("[eServiceMP3] cutlist entries need to be (pts, type)-tuples (%d %d)", PyLong_Check(ppts), PyInt_Check(ptype));
			continue;
		}
		pts_t pts = PyLong_AsLongLong(ppts);
		int type = PyInt_AsLong(ptype);
		m_cue_entries.insert(cueEntry(pts, type));
		eDebug("[eServiceMP3] adding %08llx, %d", pts, type);
	}
	m_cuesheet_changed = 1;
	m_event((iPlayableService*)this, evCuesheetChanged);
}

void eServiceMP3::setCutListEnable(int enable)
{
	m_cutlist_enabled = enable;
}

int eServiceMP3::setBufferSize(int size)
{
	m_buffer_size = size;
#ifdef ENABLE_MEDIAFWGSTREAMER
	g_object_set (G_OBJECT (m_gst_playbin), "buffer-size", m_buffer_size, NULL);
#endif
	return 0;
}

int eServiceMP3::getAC3Delay()
{
	return ac3_delay;
}

int eServiceMP3::getPCMDelay()
{
	return pcm_delay;
}

void eServiceMP3::setAC3Delay(int delay)
{
#ifdef ENABLE_MEDIAFWGSTREAMER
	ac3_delay = delay;
	if (!m_gst_playbin || m_state != stRunning)
		return;
	else
	{
		int config_delay_int = delay;

		/*
		 * NOTE: We only look for dvbmediasinks.
		 * If either the video or audio sink is of a different type,
		 * we have no chance to get them synced anyway.
		 */
		if (videoSink)
		{
			config_delay_int += eConfigManager::getConfigIntValue("config.av.generalAC3delay");
		}
		else
		{
			eDebug("[eServiceMP3] dont apply ac3 delay when no video is running!");
			config_delay_int = 0;
		}

		if (audioSink)
		{
			eTSMPEGDecoder::setHwAC3Delay(config_delay_int);
		}
	}
#endif
}

void eServiceMP3::setPCMDelay(int delay)
{
#ifdef ENABLE_MEDIAFWGSTREAMER
	pcm_delay = delay;
	if (!m_gst_playbin || m_state != stRunning)
		return;
	else
	{
		int config_delay_int = delay;

		/*
		 * NOTE: We only look for dvbmediasinks.
		 * If either the video or audio sink is of a different type,
		 * we have no chance to get them synced anyway.
		 */
		if (videoSink)
		{
			config_delay_int += eConfigManager::getConfigIntValue("config.av.generalPCMdelay");
		}
		else
		{
			eDebug("[eServiceMP3] dont apply pcm delay when no video is running!");
			config_delay_int = 0;
		}

		if (audioSink)
		{
			eTSMPEGDecoder::setHwPCMDelay(config_delay_int);
		}
	}
#endif
}
/* cuesheet CVR */
void eServiceMP3::loadCuesheet()
{
	if (!m_cuesheet_loaded)
	{
		eDebug("[eServiceMP3] loading cuesheet");
		m_cuesheet_loaded = true;
	}
	else
	{
		eDebug("[eServiceMP3] skip loading cuesheet multiple times");
		return;
	}
 
	m_cue_entries.clear();
	/* only load manual cuts if no chapter info avbl CVR */
#if GST_VERSION_MAJOR >= 1
	if (m_use_chapter_entries)
		return;
#endif

	std::string filename = m_ref.path + ".cuts";

	m_cue_entries.clear();

	FILE *f = fopen(filename.c_str(), "rb");

	if (f)
	{
		while (1)
		{
			unsigned long long where;
			unsigned int what;

			if (!fread(&where, sizeof(where), 1, f))
				break;
			if (!fread(&what, sizeof(what), 1, f))
				break;

			where = be64toh(where);
			what = ntohl(what);

			if (what > 3)
				break;

			m_cue_entries.insert(cueEntry(where, what));
		}
		fclose(f);
		eDebug("[eServiceMP3] cuts file has %zd entries", m_cue_entries.size());
	} else
		eDebug("[eServiceMP3] cutfile not found!");

	m_cuesheet_changed = 0;
	m_event((iPlayableService*)this, evCuesheetChanged);
}
/* cuesheet CVR */
void eServiceMP3::saveCuesheet()
{
	std::string filename = m_ref.path;

		/* save cuesheet only when main file is accessible. */
#if GST_VERSION_MAJOR < 1
	if (::access(filename.c_str(), R_OK) < 0)
		return;
#else
		/* save cuesheet only when main file is accessible. and no TOC chapters avbl*/
	if ((::access(filename.c_str(), R_OK) < 0) || m_use_chapter_entries)
		return;
#endif
	filename.append(".cuts");
	/* do not save to file if there are no cuts */
	/* remove the cuts file if cue is empty */
	if(m_cue_entries.begin() == m_cue_entries.end())
	{
		if (::access(filename.c_str(), F_OK) == 0)
			remove(filename.c_str());
		return;
	}

	FILE *f = fopen(filename.c_str(), "wb");

	if (f)
	{
		unsigned long long where;
		int what;

		for (std::multiset<cueEntry>::iterator i(m_cue_entries.begin()); i != m_cue_entries.end(); ++i)
		{
			where = htobe64(i->where);
			what = htonl(i->what);
			fwrite(&where, sizeof(where), 1, f);
			fwrite(&what, sizeof(what), 1, f);

		}
		fclose(f);
	}
	m_cuesheet_changed = 0;
}

