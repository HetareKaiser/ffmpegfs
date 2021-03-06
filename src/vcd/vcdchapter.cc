// -------------------------------------------------------------------------------
//  Project:		Bully's Media Player
//
//  File:		VcdChapter.cpp
//
// (c) 1984-2017 by Oblivion Software/Norbert Schlia
// All rights reserved.
// -------------------------------------------------------------------------------
//

#include "vcdchapter.h"
#include "vcdutils.h"

#include <string.h>
#include <limits.h>

VcdChapter::VcdChapter(bool is_svcd) :
    m_is_svcd(is_svcd),
    m_track_no(0),
    m_min(0),
    m_sec(0),
    m_frame(0),
    m_start_pos(0)
{

}

VcdChapter::VcdChapter(const VCDCHAPTER & VcdChapter, bool is_svcd) :
    m_is_svcd(is_svcd),
    m_track_no(VcdChapter.m_track_no),
    m_min(BCD2DEC(VcdChapter.m_msf.m_min)),
    m_sec(BCD2DEC(VcdChapter.m_msf.m_sec)),
    m_frame(BCD2DEC(VcdChapter.m_msf.m_frame)),
    m_start_pos(0)
{

}

VcdChapter::VcdChapter(int track_no, int min, int sec, int frame, bool is_svcd) :
    m_is_svcd(is_svcd),
    m_track_no(track_no),
    m_min(min),
    m_sec(sec),
    m_frame(frame),
    m_start_pos(0),
    m_end_pos(0)
{

}

VcdChapter::~VcdChapter()
{

}

int VcdChapter::read(FILE *fpi, int track_no)
{
    VCDMSF msf;
    char buffer[sizeof(SYNC)];

    // Read first sync
    if (fread(&buffer, 1, sizeof(buffer), fpi) != sizeof(buffer))
    {
        return ferror(fpi);
    }

    // Validate sync
    if (memcmp(buffer, SYNC, sizeof(buffer)))
    {
        return EIO;
    }

    // Read position block
    memset(&msf, 0, sizeof(msf));

    if (fread((char *)&msf, 1, sizeof(msf), fpi) != sizeof(msf))
    {
        return ferror(fpi);
    }

    m_track_no  = track_no;
    m_min       = BCD2DEC(msf.m_min);
    m_sec       = BCD2DEC(msf.m_sec);
    m_frame     = BCD2DEC(msf.m_frame);

    return 0;
}

bool VcdChapter::get_is_vcd() const
{
    return m_is_svcd;
}

int VcdChapter::get_track_no() const
{
    return m_track_no;
}

int VcdChapter::get_min() const
{
    return m_min;
}

int VcdChapter::get_sec() const
{
    return m_sec;
}

int VcdChapter::get_frame() const
{
    return m_frame;
}

string VcdChapter::get_filename() const
{
    char buffer[PATH_MAX + 1];

    if (m_is_svcd)
    {
        sprintf(buffer, "MPEG2/AVSEQ%02u.MPG", m_track_no - 1);
    }
    else
    {
        sprintf(buffer, "MPEGAV/AVSEQ%02u.DAT", m_track_no - 1);
    }
    return buffer;
}

uint64_t VcdChapter::get_start_pos() const
{
    return m_start_pos;
}

uint64_t VcdChapter::get_end_pos() const
{
    return m_end_pos;
}

//Conversion from MSF to LBA
//--------------------------
//As from Red book because there are 75 frames in 1 second, so,
//LBA = Minute * 60 * 75 + Second * 75 + Frame - 150
//The minus 150 is the 2 second pregap that is recorded on every CD.

//Conversion from LBA to MSF
//--------------------------
//Minute = Int((LBA + 150) / (60 * 75))
//Second = Int(LBA + 150 - Minute * 60 * 75) / 75)
//Frame = LBA + 150 - Minute * 60 * 75 - Second * 75
//Where Int() is a function that truncates the fractional part giving only the whole number part.

int VcdChapter::get_lba() const
{
    return m_frame + (m_sec + m_min * 60) * 75;
}

VcdChapter & VcdChapter::operator= (VcdChapter const & other)
{
    if (this != & other)  //oder if (*this != rhs)
    {
        m_is_svcd   = other.m_is_svcd;
        m_track_no  = other.m_track_no;
        m_min   = other.m_min;
        m_sec   = other.m_sec;
        m_frame    = other.m_frame;
        m_start_pos = other.m_start_pos;
    }

    return *this; //Referenz auf das Objekt selbst zurückgeben
}

int VcdChapter::operator==(const VcdChapter & other) const
{
    return (m_track_no == other.m_track_no &&
            m_min == other.m_min &&
            m_sec == other.m_sec &&
            m_frame == other.m_frame);
}

int VcdChapter::operator<(const VcdChapter & other) const
{
    int res;
    res = (m_track_no - other.m_track_no);

    if (res < 0)
    {
        return 1;
    }

    if (res > 0)
    {
        return 0;
    }

    res = (m_min - other.m_min);

    if (res < 0)
    {
        return 1;
    }

    if (res > 0)
    {
        return 0;
    }

    res = (m_sec - other.m_sec);

    if (res < 0)
    {
        return 1;
    }

    if (res > 0)
    {
        return 0;
    }

    res = (m_frame - other.m_frame);

    if (res < 0)
    {
        return 1;
    }

    //if (res >= 0)
    return 0;
}

int VcdChapter::operator<=(const VcdChapter & other) const
{
    if (*this == other)
    {
        return 1;
    }

    return (*this < other);
}

int VcdChapter::operator>(const VcdChapter & other) const
{
    int res;
    res = (m_track_no - other.m_track_no);

    if (res > 0)
    {
        return 1;
    }

    if (res < 0)
    {
        return 0;
    }

    res = (m_min - other.m_min);

    if (res > 0)
    {
        return 1;
    }

    if (res < 0)
    {
        return 0;
    }

    res = (m_sec - other.m_sec);

    if (res > 0)
    {
        return 1;
    }

    if (res < 0)
    {
        return 0;
    }

    res = (m_frame - other.m_frame);

    if (res > 0)
    {
        return 1;
    }

    //if (res <= 0)
    return 0;
}

int VcdChapter::operator>=(const VcdChapter & other) const
{
    if (*this == other)
    {
        return 1;
    }

    return (*this > other);
}

int VcdChapter::operator!=(const VcdChapter & other) const
{
    return (m_track_no != other.m_track_no &&
            m_min != other.m_min &&
            m_sec != other.m_sec &&
            m_frame != other.m_frame);
}
