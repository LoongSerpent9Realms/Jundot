/**************************************************************************/
/*  library_jundot_webmidi.js                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             JUNDOT ENGINE                               */
/*                        https://jundotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

const JundotWebMidi = {

	$JundotWebMidi__deps: ['$JundotRuntime'],
	$JundotWebMidi: {
		abortControllers: [],
		isListening: false,
	},

	jundot_js_webmidi_open_midi_inputs__deps: ['$JundotWebMidi'],
	jundot_js_webmidi_open_midi_inputs__proxy: 'sync',
	jundot_js_webmidi_open_midi_inputs__sig: 'iiii',
	jundot_js_webmidi_open_midi_inputs: function (pSetInputNamesCb, pOnMidiMessageCb, pDataBuffer, dataBufferLen) {
		if (JundotWebMidi.is_listening) {
			return 0; // OK
		}
		if (!navigator.requestMIDIAccess) {
			return 2; // ERR_UNAVAILABLE
		}
		const setInputNamesCb = JundotRuntime.get_func(pSetInputNamesCb);
		const onMidiMessageCb = JundotRuntime.get_func(pOnMidiMessageCb);

		JundotWebMidi.isListening = true;
		navigator.requestMIDIAccess().then((midi) => {
			const inputs = [...midi.inputs.values()];
			const inputNames = inputs.map((input) => input.name);

			const c_ptr = JundotRuntime.allocStringArray(inputNames);
			setInputNamesCb(inputNames.length, c_ptr);
			JundotRuntime.freeStringArray(c_ptr, inputNames.length);

			inputs.forEach((input, i) => {
				const abortController = new AbortController();
				JundotWebMidi.abortControllers.push(abortController);
				input.addEventListener('midimessage', (event) => {
					const status = event.data[0];
					const data = event.data.slice(1);
					const size = data.length;

					if (size > dataBufferLen) {
						throw new Error(`data too big ${size} > ${dataBufferLen}`);
					}
					HEAPU8.set(data, pDataBuffer);

					onMidiMessageCb(i, status, pDataBuffer, data.length);
				}, { signal: abortController.signal });
			});
		});

		return 0; // OK
	},

	jundot_js_webmidi_close_midi_inputs__deps: ['$JundotWebMidi'],
	jundot_js_webmidi_close_midi_inputs__proxy: 'sync',
	jundot_js_webmidi_close_midi_inputs__sig: 'v',
	jundot_js_webmidi_close_midi_inputs: function () {
		for (const abortController of JundotWebMidi.abortControllers) {
			abortController.abort();
		}
		JundotWebMidi.abortControllers = [];
		JundotWebMidi.isListening = false;
	},
};

mergeInto(LibraryManager.library, JundotWebMidi);
