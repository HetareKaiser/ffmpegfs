/*
 * Bluray Disk file I/O for ffmpegfs
 *
 * Copyright (C) 2017-2018 Norbert Schlia (nschlia@oblivion-software.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifdef USE_LIBBLURAY
#include "blurayio.h"
#include "ffmpegfs.h"
#include "ffmpeg_utils.h"

#include <libbluray/bluray.h>
#include <assert.h>

//#define MIN(a,b) (((a) < (b)) ? a : b)

blurayio::blurayio()
    : m_bd(NULL)
    , m_is_eof(false)
    , m_errno(0)
    , m_rest_size(0)
    , m_rest_pos(0)
    , m_cur_pos(0)
    , m_start_pos(0)
    , m_end_pos(-1)
    , m_title_no(0)
    , m_chapter_no(0)
    , m_angle_no(0)
{
    memset(&m_data, 0, sizeof(m_data));
}

blurayio::~blurayio()
{

}

VIRTUALTYPE blurayio::type() const
{
    return VIRTUALTYPE_BLURAY;
}

int blurayio::bufsize() const
{
    return sizeof(m_data);
}

int blurayio::open(const string & filename)
{
    const char *bdpath = NULL;
    int title_count;
    int chapter_end = -1;
    char *keyfile = NULL;
    BLURAY_TITLE_INFO *ti;

    set_path(filename);

    bdpath = path.c_str();

    if (get_virtualfile() != NULL)
    {
        m_title_no      = get_virtualfile()->bluray.m_title_no - 1;
        m_chapter_no    = get_virtualfile()->bluray.m_chapter_no - 1;
        m_angle_no      = get_virtualfile()->bluray.m_angle_no - 1;
    }
    else
    {
        m_title_no      = 0;
        m_chapter_no    = 0;
        m_angle_no      = 0;
    }

    chapter_end = m_chapter_no + 1;
	
    ffmpegfs_debug(bdpath, "Opening input Bluray.");

    m_bd = bd_open(bdpath, keyfile);
    if (m_bd == NULL)
    {
        ffmpegfs_error(bdpath, "Failed to open disc.");
        return 1;
    }

    title_count = bd_get_titles(m_bd, TITLES_RELEVANT, 0);
    if (title_count <= 0)
    {
        ffmpegfs_error(bdpath, "No titles found.");
        return 1;
    }

//    if (m_title_no >= 0)
    {
        if (!bd_select_title(m_bd, m_title_no))
        {
            ffmpegfs_error(bdpath, "Failed to open title: %d", m_title_no);
            return 1;
        }
        ti = bd_get_title_info(m_bd, m_title_no, m_angle_no);

    }

    if (m_angle_no >= ti->angle_count)
    {
        ffmpegfs_warning(bdpath, "Invalid angle %d > angle count %d. Using angle 1.", m_angle_no+1, ti->angle_count);
        m_angle_no = 0;
    }

    bd_select_angle(m_bd, m_angle_no);

    if (m_chapter_no >= ti->chapter_count)
    {
        ffmpegfs_error(bdpath, "First chapter %d > chapter count %d", m_chapter_no+1, ti->chapter_count);
        return 1;
    }

    if (chapter_end >= (int)ti->chapter_count)
    {
        chapter_end = -1;
    }

    if (chapter_end >= 0)
    {
        m_end_pos = bd_chapter_pos(m_bd, chapter_end);
    }
    else
    {
        m_end_pos = bd_get_title_size(m_bd);
    }

    bd_free_title_info(ti);

    m_start_pos = bd_seek_chapter(m_bd, m_chapter_no);

    m_rest_size = 0;
    m_rest_pos = 0;

    return 0;
}

int blurayio::read(void * dataX, int size)
{
    int result_len = 0;

    if (m_rest_size)
    {
        result_len = (int)m_rest_size;

        assert(m_rest_size < (size_t)size);

        memcpy(dataX, &m_data[m_rest_pos], m_rest_size);

        m_rest_size = m_rest_pos = 0;

        return result_len;
    }

    m_cur_pos = bd_tell(m_bd);
    if (m_end_pos < 0 || m_cur_pos < m_end_pos)
    {
        int bytes;
        int XXsize;

        XXsize = (int)sizeof(m_data);
        if (XXsize > (m_end_pos - m_cur_pos))
        {
            XXsize = (int)(m_end_pos - m_cur_pos);
        }
        bytes = bd_read(m_bd, m_data, XXsize);
        if (bytes <= 0)
        {
            ffmpegfs_error(path.c_str(), "bd_read fail ret = %i", bytes);
            return 0;
        }
        m_cur_pos = bd_tell(m_bd);

        if (bytes > size)
        {
            result_len = size;
            memcpy(dataX, m_data, result_len);

            m_rest_size = bytes - size;
            m_rest_pos = size;
        }
        else
        {
            result_len = bytes;
            memcpy(dataX, m_data, result_len);
        }
    }

    return result_len;
}

int blurayio::error() const
{
    return m_errno;
}

int blurayio::duration() const
{
    return -1;
}

size_t blurayio::size() const
{
    return (m_end_pos - m_start_pos);
}

size_t blurayio::tell() const
{
    return (bd_tell(m_bd) - m_start_pos);
}

int blurayio::seek(long offset, int whence)
{
    long int seek_pos;
    switch (whence)
    {
    case SEEK_SET:
    {
        seek_pos = m_start_pos + offset;
        break;
    }
    case SEEK_CUR:
    {
        seek_pos = m_start_pos + offset + bd_tell(m_bd);
        break;
    }
    case SEEK_END:
    {
        seek_pos = m_end_pos - offset;
        break;
    }
    default:
    {
        errno = EINVAL;
        return 0;
    }
    }

    return (bd_seek(m_bd, seek_pos) != seek_pos);
}

bool blurayio::eof() const
{
    return (m_cur_pos >= m_end_pos);
}

void blurayio::close()
{
    bd_close(m_bd);
}

#endif // USE_LIBBLURAY
