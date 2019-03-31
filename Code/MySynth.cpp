#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <poll.h>
#include <thread>
#include <mutex>
#include <queue>
#include <chrono>
#include <iostream>

#include <alsa/asoundlib.h>

#include <stk/Noise.h>
#include <stk/Iir.h>

#define PLAYBACK_DEVICE "default"
#define CAPTURE_DEVICE "default"

#define BUF_SIZE 4096*2
#define SAMPLE_RATE 44100

// g++ MySynth.cpp -o MySynth.o -lasound
snd_pcm_t *capture_handle;
snd_pcm_t *playback_handle;

//FIFO, source->capture_device, dest->effect function
std::queue<short> capture_fifo;
//FIFO, source->effects function, dest->playback device
std::queue<short> playback_fifo;

//mutexes to protect FIFO buffers
std::mutex capture_mutex;
std::mutex playback_mutex;

struct confData {
	snd_pcm_hw_params_t *hw_playback_params;
	snd_pcm_sw_params_t *sw_playback_params;
	snd_pcm_hw_params_t *hw_capture_params;
	snd_pcm_sw_params_t *sw_capture_params;
	unsigned int sample_rate;
};

void applyEffect(short buf[])
{
	//init StkFrames and convert buf to fit in range -1.0 to 1.0
	stk::StkFrames output( BUF_SIZE, 1 );
	for (int i=0; i < BUF_SIZE; i++){
		output[i] = static_cast<double>(buf[i])/0x8000;
	}

	//do filtering
  	std::vector<stk::StkFloat> numerator( 5, 0.1 ); // create and initialize numerator coefficients
  	std::vector<stk::StkFloat> denominator;         // create empty denominator coefficients
  	denominator.push_back( 1.0 );              // populate our denomintor values
  	denominator.push_back( 0.3 );
  	denominator.push_back( -0.5 );
  	stk::Iir filter( numerator, denominator );
  	filter.tick( output );

	// fill buffer with filtered values
	for (int i=0; i < BUF_SIZE; i++){
		buf[i]=static_cast<short>(output[i]*0x8000);
	}

}

int playback_callback(snd_pcm_sframes_t nframes, short buf[])
{

	int err;

	//printf("playback callback called with %lu frames\n", nframes);

	err = snd_pcm_writei(playback_handle, buf, nframes);
	if (err < 0) {
		fprintf(stderr, "write failed (%s)\n", snd_strerror(err));
	}

	return err;
}

int capture_callback(snd_pcm_sframes_t nframes, short buf[])
{

	int err;

	//printf("capture callback called with %lu frames\n", nframes);

	/* ... fill buf with data ... */
	err = snd_pcm_readi(capture_handle, buf, nframes);
	if (err != nframes)	{
		fprintf(stderr, "read failed (%s)\n", snd_strerror(err));
	}

	return err;
}

void readThread()
{
	int frames_captured;
	snd_pcm_sframes_t frames_to_read = BUF_SIZE;
	int err;
	short buf[BUF_SIZE];
	int delay = (BUF_SIZE*1000/SAMPLE_RATE)-10; //delay in milliseconds

	while (1) {
		/* wait till the capture device is ready for data, or 1 second
		 * has elapsed.
		 */

		err = snd_pcm_wait(capture_handle, 1000);
		if (err < 0) {
			fprintf(stderr, "poll failed (%s)\n", strerror(errno));
			break;
		}

		// capture data
		frames_captured = capture_callback(frames_to_read, buf);
		if (frames_captured != frames_to_read) {
			fprintf(stderr, "capture callback failed\n");
			break;
		}

		capture_mutex.lock();
		for (int i = 0; i < BUF_SIZE; i++){
			capture_fifo.push(buf[i]);
		}
		std::cout << "read thread: capture fifo size " << capture_fifo.size() << std::endl;
		capture_mutex.unlock();
		std::this_thread::sleep_for(std::chrono::milliseconds(delay));

	}
}

void effectThread()
{
	short buf[BUF_SIZE];
	int delay = (BUF_SIZE*1000/SAMPLE_RATE)-10; //delay in milliseconds

	while(1){

		if (capture_fifo.size() >= BUF_SIZE){
			capture_mutex.lock();
			for (int i = 0; i < BUF_SIZE; i++){
				buf[i] = capture_fifo.front();
				capture_fifo.pop();
			}
			std::cout << "effect thread: capture fifo size " << capture_fifo.size() << std::endl;
			capture_mutex.unlock();
			//applyEffect(buf);


			playback_mutex.lock();
			for (int i = 0; i < BUF_SIZE; i++){
				playback_fifo.push(buf[i]);
			}
			std::cout << "effect thread: playback fifo size " << playback_fifo.size() << std::endl;
			playback_mutex.unlock();
			std::this_thread::sleep_for(std::chrono::milliseconds(delay));
		}
	}
}

void writeThread()
{
	snd_pcm_sframes_t frames_to_deliver;
	int err;
	int frames_played;
	short buf[BUF_SIZE];
	int delay = (BUF_SIZE*1000/SAMPLE_RATE); //delay in milliseconds


	while (1) {

		/* wait till the playback device is ready for data, or 1 second
		 * has elapsed.
		 */
		err = snd_pcm_wait(playback_handle, 1000);
		if (err  < 0) {
			fprintf(stderr, "poll failed (%s)\n", strerror(errno));
			break;
		}

		// prpare playback device

		err = snd_pcm_prepare (playback_handle);
		if (err < 0) {
			fprintf (stderr, "cannot prepare playback interface for use (%s)\n",
				 snd_strerror (err));
			exit (1);
		}

		/*
		if(playback_fifo.size() < BUF_SIZE){
			frames_to_deliver = playback_fifo.size();
		} else {
			frames_to_deliver = BUF_SIZE;
		}*/




		/* deliver the data */
		if (playback_fifo.size() > (BUF_SIZE*2)){

			playback_mutex.lock();
			for (int i = 0; i < BUF_SIZE; i++){
				buf[i] = playback_fifo.front();
				playback_fifo.pop();
			}
			std::cout << "write thread: playback fifo size " << playback_fifo.size() << std::endl;
			playback_mutex.unlock();

			frames_played = playback_callback(BUF_SIZE, buf);
			if (frames_played != BUF_SIZE) {
				fprintf(stderr, "playback callback failed\n");
				//snd_pcm_recover (playback_handle, BUF_SIZE, 0);
				break;
			}
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(delay));

	}
}

int open_and_init(struct confData *conf)
{
	int err;

	// open playback device
	err = snd_pcm_open(&playback_handle, PLAYBACK_DEVICE, SND_PCM_STREAM_PLAYBACK, 0);
	if (err < 0) {
		fprintf(stderr, "cannot open output audio device %s (%s)\n",
				PLAYBACK_DEVICE,
				snd_strerror(err));
		exit(1);
	}
	// open capture device
	err = snd_pcm_open(&capture_handle, CAPTURE_DEVICE, SND_PCM_STREAM_CAPTURE, 0);
	if (err < 0) {
		fprintf(stderr, "cannot open input audio device %s (%s)\n",
				CAPTURE_DEVICE,
				snd_strerror(err));
		exit(1);
	}

	//allocate hw params
	err = snd_pcm_hw_params_malloc(&conf->hw_playback_params);
	if (err < 0) {
		fprintf(stderr, "cannot allocate playback hardware parameter structure (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_hw_params_malloc(&conf->hw_capture_params);
	if (err < 0) {
		fprintf(stderr, "cannot allocate capture hardware parameter structure (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	// init hw_param structures
	err = snd_pcm_hw_params_any(playback_handle, conf->hw_playback_params);
	if (err < 0) {
		fprintf(stderr, "cannot initialize playback hardware parameter structure (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_hw_params_any(capture_handle, conf->hw_capture_params);
	if (err < 0) {
		fprintf(stderr, "cannot initialize capture hardware parameter structure (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	//set access type
	err = snd_pcm_hw_params_set_access(playback_handle, conf->hw_playback_params, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err < 0) {
		fprintf(stderr, "cannot set access type (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_hw_params_set_access(capture_handle, conf->hw_capture_params, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err < 0) {
		fprintf(stderr, "cannot set access type (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	//set sample formats
	err = snd_pcm_hw_params_set_format(playback_handle, conf->hw_playback_params, SND_PCM_FORMAT_S16_LE);
	if (err < 0) {
		fprintf(stderr, "cannot set sample format (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_hw_params_set_format(capture_handle, conf->hw_capture_params, SND_PCM_FORMAT_S16_LE);
	if (err < 0) {
		fprintf(stderr, "cannot set sample format (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	//set sample rate
	err = snd_pcm_hw_params_set_rate_near(playback_handle, conf->hw_playback_params, &conf->sample_rate, 0);
	if (err < 0) {
		fprintf(stderr, "cannot set sample rate (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_hw_params_set_rate_near(capture_handle, conf->hw_capture_params, &conf->sample_rate, 0);
	if (err < 0) {
		fprintf(stderr, "cannot set sample rate (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	// set chanel count
	err = snd_pcm_hw_params_set_channels(playback_handle, conf->hw_playback_params, 1);
	if (err < 0) {
		fprintf(stderr, "cannot set channel count (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_hw_params_set_channels(capture_handle, conf->hw_capture_params, 1);
	if (err < 0) {
		fprintf(stderr, "cannot set channel count (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	// set parameters
	err = snd_pcm_hw_params(playback_handle, conf->hw_playback_params);
	if (err < 0) {
		fprintf(stderr, "cannot set parameters (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_hw_params(capture_handle, conf->hw_capture_params);
	if (err < 0) {
		fprintf(stderr, "cannot set parameters (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	//free params
	snd_pcm_hw_params_free(conf->hw_playback_params);
	snd_pcm_hw_params_free(conf->hw_capture_params);

	/* tell ALSA to wake us up whenever 4096 or more frames
		   of playback data can be delivered. Also, tell
		   ALSA that we'll start the device ourselves.
		*/

	err = snd_pcm_sw_params_malloc(&conf->sw_playback_params);
	if (err < 0) {
		fprintf(stderr, "cannot allocate software parameters structure (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_sw_params_current(playback_handle, conf->sw_playback_params);
	if (err < 0) {
		fprintf(stderr, "cannot initialize software parameters structure (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_sw_params_set_avail_min(playback_handle, conf->sw_playback_params, 4096);
	if (err < 0) {
		fprintf(stderr, "cannot set minimum available count (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_sw_params_set_start_threshold(playback_handle, conf->sw_playback_params, 0U);
	if (err < 0) {
		fprintf(stderr, "cannot set start mode (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_sw_params(playback_handle, conf->sw_playback_params);
	if (err < 0) {
		fprintf(stderr, "cannot set software parameters (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	/* the interface will interrupt the kernel every 4096 frames, and ALSA
		   will wake up this program very soon after that.
		*/

	err = snd_pcm_prepare(playback_handle);
	if (err < 0) {
		fprintf(stderr, "cannot prepare audio interface for use (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	err = snd_pcm_prepare(capture_handle);
	if (err < 0) {
		fprintf(stderr, "cannot prepare audio interface for use (%s)\n",
				snd_strerror(err));
		exit(1);
	}

	return 0;
}

int main(int argc, char *argv[])
{

	struct confData conf;
	conf.sample_rate = (unsigned int)SAMPLE_RATE; // set sample rate


	open_and_init(&conf);

	std::thread read_thread (readThread);
	std::thread effect_thread (effectThread);
	std::thread write_thread (writeThread);


	read_thread.join();
	effect_thread.join();
	write_thread.join();

	//snd_pcm_close(playback_handle);
	//exit(0);
	//while(1){};
}



