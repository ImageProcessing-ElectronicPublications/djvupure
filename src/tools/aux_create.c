/*
BSD 2-Clause License

Copyright (c) 2023, Mikhail Morozov

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "../../include/djvupure.h"

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "aux_create.h"

djvupure_chunk_t * CreateInfoChunkFromParams(wchar_t *params)
{
	djvupure_page_info_t info;
	size_t i = 0;
	bool is_gamma_set = false, is_rotation_set = false;

	memset(&info, 0, sizeof(djvupure_page_info_t));

	// Parameters are width,height,dpi,rotation,gamma

	// Read width
	while(params[i] != ',' && params[i] != 0) {
		info.width *= 10;
		info.width += (params[i]-'0')%10;
		i++;
	}
	if(params[i] == ',') i++;

	// Read height
	while(params[i] != ',' && params[i] != 0) {
		info.height *= 10;
		info.height += (params[i]-'0')%10;
		i++;
	}
	if(params[i] == ',') i++;

	// Read dpi
	while(params[i] != ',' && params[i] != 0) {
		info.dpi *= 10;
		info.dpi += (params[i]-'0')%10;
		i++;
	}
	if(params[i] == ',') i++;

	// Read rotation
	while(params[i] != ',' && params[i] != 0) {
		is_rotation_set = true;
		info.rotation *= 10;
		info.rotation += (params[i]-'0')%10;
		i++;
	}
	if(params[i] == ',') i++;

	// Read gamma
	while(params[i] != ',' && params[i] != 0) {
		is_gamma_set = true;
		info.gamma *= 10;
		info.gamma += (params[i]-'0')%10;
		i++;
	}
	if(params[i] == ',') i++;

	if(!is_gamma_set) info.gamma = 22;
	if(!is_rotation_set) info.rotation = 1;

	return djvupureInfoCreate(info);
}

djvupure_chunk_t * CreateRawChunkFromFile(uint8_t sign[4], wchar_t *chunk_filename)
{
	djvupure_io_callback_t io;
	djvupure_chunk_t *chunk = 0;
	void *chunk_fctx = 0;
	void *chunk_data = 0;
	int64_t chunk_data_len;

	djvupureFileSetIoCallbacks(&io);
			
	chunk_fctx = djvupureFileOpenW(chunk_filename, false);
	if(!chunk_fctx) {
		wprintf(L"Can't open file \"%ls\" for chunk\n", chunk_filename);

		goto FINAL;
	}

	if(io.callback_seek(chunk_fctx, 0, DJVUPURE_IO_SEEK_END)) {
		wprintf(L"Can't process file \"%ls\" for chunk\n", chunk_filename);
		goto FINAL;
	}

	chunk_data_len = io.callback_tell(chunk_fctx);
	if(chunk_data_len > SIZE_MAX) {
		wprintf(L"File \"%ls\" is too large\n", chunk_filename);
		goto FINAL;
	}

	if(io.callback_seek(chunk_fctx, 0, DJVUPURE_IO_SEEK_SET)) {
		wprintf(L"Can't process file \"%ls\" for chunk\n", chunk_filename);
		goto FINAL;
	}

	chunk_data = malloc((size_t)chunk_data_len);
	if(!chunk_data) {
		wprintf(L"File \"%ls\" is too large\n", chunk_filename);
		goto FINAL;
	}

	if(io.callback_read(chunk_fctx, chunk_data, (size_t)chunk_data_len) != chunk_data_len) {
		wprintf(L"Can't process file \"%ls\" for chunk\n", chunk_filename);
		goto FINAL;
	}

	chunk = djvupureRawChunkCreate(sign, chunk_data, (size_t)chunk_data_len);

FINAL:
	if(chunk_fctx) djvupureFileClose(chunk_fctx);
	if(chunk_data) free(chunk_data);

	return chunk;
}

void CreateIW44ChunkFromFile(djvupure_chunk_t *page, uint8_t sign[4], wchar_t *chunk_filename, size_t chunks_to_copy)
{
	djvupure_io_callback_t io;
	djvupure_chunk_t *chunk = 0, *pm44 = 0;
	void *fctx = 0;
	size_t nof_pm44_subchunks;
	const uint8_t pm44_sign[4] = { 'P', 'M', '4', '4' };

	djvupureFileSetIoCallbacks(&io);

	fctx = djvupureFileOpenW(chunk_filename, false);
	if(!fctx) {
		wprintf(L"Can't open file \"%ls\" for chunk\n", chunk_filename);

		goto FINAL;
	}

	pm44 = djvupureContainerRead(&io, fctx);
	if(!pm44) {
		wprintf(L"Can't open file \"%ls\" for chunk\n", chunk_filename);

		goto FINAL;
	}

	if(!djvupureContainerIs(pm44, pm44_sign)) {
		wprintf(L"File \"%ls\" is not a IFF85 PM44 file\n", chunk_filename);

		goto FINAL;
	}

	nof_pm44_subchunks = djvupureContainerSize(pm44);
	if(chunks_to_copy == 0) chunks_to_copy = nof_pm44_subchunks;

	for(size_t i = 0; i < nof_pm44_subchunks && chunks_to_copy > 0; i++) {
		djvupure_chunk_t *subchunk, *iw44;
		void *data;
		size_t data_len, page_subindex;

		subchunk = djvupureContainerGetSubchunk(pm44, i);
		if(!subchunk) continue;

		if(memcmp(subchunk->sign, pm44_sign, 4)) continue;

		djvupureRawChunkGetDataPointer(subchunk, &data, &data_len);

		iw44 = djvupureRawChunkCreate(sign, data, data_len);
		if(!iw44) {
			wprintf(L"Can't append chunk %.4hs\n", sign);

			continue;
		}

		page_subindex = djvupureContainerSize(page);
		if(!djvupureContainerInsertChunk(page, iw44, page_subindex)) {
			wprintf(L"Can't append chunk %.4hs\n", sign);
			djvupureChunkFree(iw44);

			continue;
		}

		chunks_to_copy--;
	}

FINAL:
	if(fctx) djvupureFileClose(fctx);
	if(pm44) djvupureChunkFree(pm44);
	if(chunk) djvupureChunkFree(chunk);
}