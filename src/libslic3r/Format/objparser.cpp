///|/ Copyright (c) Prusa Research 2017 - 2021 Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, Tomáš Mészáros @tamasmeszaros, Enrico Turri @enricoturri1966
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include <cstdlib>
#include <cstring>

#include <boost/log/trivial.hpp>
#include <boost/nowide/cstdio.hpp>

#include "objparser.hpp"

#include "libslic3r/LocalesUtils.hpp"
#include "fast_float/fast_float.h"

namespace ObjParser {

// To fix issues with obj loading on macOS Sonoma, we use the following function instead of strtod that
// was used before. Apparently the locales are not handled as they should. We already saw this before in
// https://github.com/prusa3d/PrusaSlicer/issues/10380.
static double strtod_clocale(const char* str, char const** str_end)
{
	double val = 0.;
	auto [pend, ec] = fast_float::from_chars(str, *str_end, val);
	if (pend != str && ec != std::errc::result_out_of_range)
		*str_end = pend; // success
	else
		*str_end = str;
	return val;
}

static bool obj_parseline(const char *line, ObjData &data)
{
#define EATWS() while (*line == ' ' || *line == '\t') ++ line

	if (*line == 0)
		return true;

    assert(Slic3r::is_decimal_separator_point());

	// Ignore whitespaces at the beginning of the line.
	//FIXME is this a good idea?
	EATWS();

	char c1 = *line ++;
	switch (c1) {
	case '#':
		// Comment, ignore the rest of the line.
		break;
	case 'v':
	{
		// Parse vertex geometry (position, normal, texture coordinates)
		char c2 = *line ++;
		switch (c2) {
		case 't':
		{
			// vt - vertex texture parameter
			// u v [w], w == 0 (or w == 1)
			char c2 = *line ++;
			if (c2 != ' ' && c2 != '\t')
				return false;
			EATWS();
			const char *endptr = 0;
			double u = strtod_clocale(line, &endptr);
			if (endptr == 0 || (*endptr != ' ' && *endptr != '\t'))
				return false;
			line = endptr;
			EATWS();
			double v = 0;
			if (*line != 0) {
				v = strtod_clocale(line, &endptr);
				if (endptr == 0 || (*endptr != ' ' && *endptr != '\t' && *endptr != 0))
					return false;
				line = endptr;
				EATWS();
			}
			double w = 0;
			if (*line != 0) {
				w = strtod_clocale(line, &endptr);
				if (endptr == 0 || (*endptr != ' ' && *endptr != '\t' && *endptr != 0))
					return false;
				line = endptr;
				EATWS();
			}
			if (*line != 0)
				return false;
			data.textureCoordinates.push_back((float)u);
			data.textureCoordinates.push_back((float)v);
			data.textureCoordinates.push_back((float)w);
			break;
		}
		case 'n':
		{
			// vn - vertex normal
			// x y z
			char c2 = *line ++;
			if (c2 != ' ' && c2 != '\t')
				return false;
			EATWS();
			const char *endptr = 0;
			double x = strtod_clocale(line, &endptr);
			if (endptr == 0 || (*endptr != ' ' && *endptr != '\t'))
				return false;
			line = endptr;
			EATWS();
			double y = strtod_clocale(line, &endptr);
			if (endptr == 0 || (*endptr != ' ' && *endptr != '\t'))
				return false;
			line = endptr;
			EATWS();
			double z = strtod_clocale(line, &endptr);
			if (endptr == 0 || (*endptr != ' ' && *endptr != '\t' && *endptr != 0))
				return false;
			line = endptr;
			EATWS();
			if (*line != 0)
				return false;
			data.normals.push_back((float)x);
			data.normals.push_back((float)y);
			data.normals.push_back((float)z);
			break;
		}
		case 'p':
		{
			// vp - vertex parameter
			char c2 = *line ++;
			if (c2 != ' ' && c2 != '\t')
				return false;
			EATWS();
			const char *endptr = 0;
			double u = strtod_clocale(line, &endptr);
			if (endptr == 0 || (*endptr != ' ' && *endptr != '\t' && *endptr != 0))
				return false;
			line = endptr;
			EATWS();
			double v = strtod_clocale(line, &endptr);
			if (endptr == 0 || (*endptr != ' ' && *endptr != '\t' && *endptr != 0))
				return false;
			line = endptr;
			EATWS();
			double w = 0;
			if (*line != 0) {
				w = strtod_clocale(line, &endptr);
				if (endptr == 0 || (*endptr != ' ' && *endptr != '\t' && *endptr != 0))
					return false;
				line = endptr;
				EATWS();
			}
			if (*line != 0)
				return false;
			data.parameters.push_back((float)u);
			data.parameters.push_back((float)v);
			data.parameters.push_back((float)w);
			break;
		}
		default:
		{
			// v - vertex geometry
			if (c2 != ' ' && c2 != '\t')
				return false;
			EATWS();
			const char *endptr = 0;
			double x = strtod_clocale(line, &endptr);
			if (endptr == 0 || (*endptr != ' ' && *endptr != '\t'))
				return false;
			line = endptr;
			EATWS();
			double y = strtod_clocale(line, &endptr);
			if (endptr == 0 || (*endptr != ' ' && *endptr != '\t'))
				return false;
			line = endptr;
			EATWS();
			double z = strtod_clocale(line, &endptr);
			if (endptr == 0 || (*endptr != ' ' && *endptr != '\t' && *endptr != 0))
				return false;
			line = endptr;
			EATWS();
			double w = 1.0;
			if (*line != 0) {
				w = strtod_clocale(line, &endptr);
				if (endptr == 0 || (*endptr != ' ' && *endptr != '\t' && *endptr != 0))
					return false;
				line = endptr;
				EATWS();
			}
            // the following check is commented out because there may be obj files containing extra data, as those generated by Meshlab,
            // see https://dev.prusa3d.com/browse/SPE-1019 for an example,
            // and this would lead to a crash because no vertex would be stored 
//            if (*line != 0)
//                return false;
            data.coordinates.push_back((float)x);
			data.coordinates.push_back((float)y);
			data.coordinates.push_back((float)z);
			data.coordinates.push_back((float)w);
			break;
		}
		}
		break;
	}
	case 'f':
	{
		// face
		EATWS();
		if (*line == 0)
			return false;

		// current vertex to be parsed
		ObjVertex vertex;
		char *endptr = 0;
		while (*line != 0) {
			// Parse a single vertex reference.
			vertex.coordIdx			= 0;
			vertex.normalIdx		= 0;
			vertex.textureCoordIdx	= 0;
			vertex.coordIdx = strtol(line, &endptr, 10);
			// Coordinate has to be defined
			if (endptr == 0 || (*endptr != ' ' && *endptr != '\t' && *endptr != '/' && *endptr != 0))
				return false;
			line = endptr;
			if (*line == '/') {
				++ line;
				// Texture coordinate index may be missing after a 1st slash, but then the normal index has to be present.
				if (*line != '/') {
					// Parse the texture coordinate index.
					vertex.textureCoordIdx = strtol(line, &endptr, 10);
					if (endptr == 0 || (*endptr != ' ' && *endptr != '\t' && *endptr != '/' && *endptr != 0))
						return false;
					line = endptr;
				}
				if (*line == '/') {
					// Parse normal index.
					++ line;
					vertex.normalIdx = strtol(line, &endptr, 10);
					if (endptr == 0 || (*endptr != ' ' && *endptr != '\t' && *endptr != 0))
						return false;
					line = endptr;
				}
			}
			if (vertex.coordIdx < 0)
                vertex.coordIdx += (int)data.coordinates.size() / 4;
            else
				-- vertex.coordIdx;
			if (vertex.normalIdx < 0)
                vertex.normalIdx += (int)data.normals.size() / 3;
            else
				-- vertex.normalIdx;
			if (vertex.textureCoordIdx < 0)
                vertex.textureCoordIdx += (int)data.textureCoordinates.size() / 3;
            else
				-- vertex.textureCoordIdx;
			data.vertices.push_back(vertex);
			EATWS();
		}
		vertex.coordIdx			= -1;
		vertex.normalIdx		= -1;
		vertex.textureCoordIdx	= -1;
		data.vertices.push_back(vertex);
		break;
	}
	case 'm':
	{
		if (*(line ++) != 't' ||
			*(line ++) != 'l' ||
			*(line ++) != 'l' ||
			*(line ++) != 'i' ||
			*(line ++) != 'b')
			return false;
		// mtllib [external .mtl file name]
		// printf("mtllib %s\r\n", line);
		EATWS();
		data.mtllibs.push_back(std::string(line));
		break;
	}
	case 'u':
	{
		if (*(line ++) != 's' ||
			*(line ++) != 'e' ||
			*(line ++) != 'm' ||
			*(line ++) != 't' ||
			*(line ++) != 'l')
			return false;
		// usemtl [material name]
		// printf("usemtl %s\r\n", line);
		EATWS();
		ObjUseMtl usemtl;
        usemtl.vertexIdxFirst = (int)data.vertices.size();
        usemtl.name = line;
		data.usemtls.push_back(usemtl);
		break;
	}
	case 'o':
	{
		// o [object name]
		EATWS();
		while (*line != ' ' && *line != '\t' && *line != 0)
			++ line;
		// copy name to line.
		EATWS();
		if (*line != 0)
			return false;
		ObjObject object;
        object.vertexIdxFirst = (int)data.vertices.size();
        object.name = line;
		data.objects.push_back(object);
		break;
	}
	case 'g':
	{
		// g [group name]
		// printf("group %s\r\n", line);
		ObjGroup group;
        group.vertexIdxFirst = (int)data.vertices.size();
        group.name = line;
		data.groups.push_back(group);
		break;
	}
	case 's':
	{
		// s 1 / off
		char c2 = *line ++;
		if (c2 != ' ' && c2 != '\t')
			return false;
		EATWS();
		char *endptr = 0;
		long g = strtol(line, &endptr, 10);
		if (endptr == 0 || (*endptr != ' ' && *endptr != '\t' && *endptr != 0))
			return false;
		line = endptr;
		EATWS();
		if (*line != 0)
			return false;
		ObjSmoothingGroup group;
        group.vertexIdxFirst = (int)data.vertices.size();
        group.smoothingGroupID = g;
		data.smoothingGroups.push_back(group);
		break;
	}
	default:
    	BOOST_LOG_TRIVIAL(error) << "ObjParser: Unknown command: " << c1;
		break;
	}

	return true;
}

bool objparse(const char *path, ObjData &data)
{
    Slic3r::CNumericLocalesSetter locales_setter;

	FILE *pFile = boost::nowide::fopen(path, "rt");
	if (pFile == 0)
		return false;

	try {
		char buf[65536 * 2];
		size_t len = 0;
		size_t lenPrev = 0;
		while ((len = ::fread(buf + lenPrev, 1, 65536, pFile)) != 0) {
			len += lenPrev;
			size_t lastLine = 0;
			for (size_t i = 0; i < len; ++ i)
				if (buf[i] == '\r' || buf[i] == '\n') {
					buf[i] = 0;
					char *c = buf + lastLine;
					while (*c == ' ' || *c == '\t')
						++ c;
					//FIXME check the return value and exit on error?
					// Will it break parsing of some obj files?
					obj_parseline(c, data);
					lastLine = i + 1;
				}
			lenPrev = len - lastLine;
			if (lenPrev > 65536) {
		    	BOOST_LOG_TRIVIAL(error) << "ObjParser: Excessive line length";
				::fclose(pFile);
				return false;
			}
			memmove(buf, buf + lastLine, lenPrev);
		}
    }
    catch (std::bad_alloc&) {
    	BOOST_LOG_TRIVIAL(error) << "ObjParser: Out of memory";
	}
	::fclose(pFile);

	// printf("vertices: %d\r\n", data.vertices.size() / 4);
	// printf("coords: %d\r\n", data.coordinates.size());
	return true;
}

bool objparse(std::istream &stream, ObjData &data)
{
    Slic3r::CNumericLocalesSetter locales_setter;
    
    try {
        char buf[65536 * 2];
        size_t len = 0;
        size_t lenPrev = 0;
        while ((len = size_t(stream.read(buf + lenPrev, 65536).gcount())) != 0) {
            len += lenPrev;
            size_t lastLine = 0;
            for (size_t i = 0; i < len; ++ i)
                if (buf[i] == '\r' || buf[i] == '\n') {
                    buf[i] = 0;
                    char *c = buf + lastLine;
                    while (*c == ' ' || *c == '\t')
                        ++ c;
                    obj_parseline(c, data);
                    lastLine = i + 1;
                }
            lenPrev = len - lastLine;
            memmove(buf, buf + lastLine, lenPrev);
        }
    }
    catch (std::bad_alloc&) {
    	BOOST_LOG_TRIVIAL(error) << "ObjParser: Out of memory";
    	return false;
    }
    
    return true;
}

template<typename T> 
bool savevector(FILE *pFile, const std::vector<T> &v)
{
	size_t cnt = v.size();
	::fwrite(&cnt, 1, sizeof(cnt), pFile);
	//FIXME sizeof(T) works for data types leaving no gaps in the allocated vector because of alignment of the T type.
	if (! v.empty())
		::fwrite(&v.front(), 1, sizeof(T) * cnt, pFile);
	return true;
}

bool savevector(FILE *pFile, const std::vector<std::string> &v)
{
	size_t cnt = v.size();
	::fwrite(&cnt, 1, sizeof(cnt), pFile);
	for (size_t i = 0; i < cnt; ++ i) {
		size_t len = v[i].size();
		::fwrite(&len, 1, sizeof(cnt), pFile);
		::fwrite(v[i].c_str(), 1, len, pFile);
	}
	return true;
}

template<typename T>
bool savevectornameidx(FILE *pFile, const std::vector<T> &v)
{
	size_t cnt = v.size();
	::fwrite(&cnt, 1, sizeof(cnt), pFile);
	for (size_t i = 0; i < cnt; ++ i) {
		::fwrite(&v[i].vertexIdxFirst, 1, sizeof(int), pFile);
		size_t len = v[i].name.size();
		::fwrite(&len, 1, sizeof(cnt), pFile);
		::fwrite(v[i].name.c_str(), 1, len, pFile);
	}
	return true;
}

template<typename T> 
bool loadvector(FILE *pFile, std::vector<T> &v)
{
	v.clear();
	size_t cnt = 0;
	if (::fread(&cnt, sizeof(cnt), 1, pFile) != 1)
		return false;
	//FIXME sizeof(T) works for data types leaving no gaps in the allocated vector because of alignment of the T type.
	if (cnt != 0) {
		v.assign(cnt, T());
		if (::fread(&v.front(), sizeof(T), cnt, pFile) != cnt)
			return false;
	}
	return true;
}

bool loadvector(FILE *pFile, std::vector<std::string> &v)
{
	v.clear();
	size_t cnt = 0;
	if (::fread(&cnt, sizeof(cnt), 1, pFile) != 1)
		return false;
	v.reserve(cnt);
	for (size_t i = 0; i < cnt; ++ i) {
		size_t len = 0;
		if (::fread(&len, sizeof(len), 1, pFile) != 1)
			return false;
		std::string s(" ", len);
		if (::fread(s.data(), 1, len, pFile) != len)
			return false;
		v.push_back(std::move(s));
	}
	return true;
}

template<typename T>
bool loadvectornameidx(FILE *pFile, std::vector<T> &v)
{
	v.clear();
	size_t cnt = 0;
	if (::fread(&cnt, sizeof(cnt), 1, pFile) != 1)
		return false;
	v.assign(cnt, T());
	for (size_t i = 0; i < cnt; ++ i) {
		if (::fread(&v[i].vertexIdxFirst, sizeof(int), 1, pFile) != 1)
			return false;
		size_t len = 0;
		if (::fread(&len, sizeof(len), 1, pFile) != 1)
			return false;
		v[i].name.assign(" ", len);
		if (::fread(v[i].name.data(), 1, len, pFile) != len)
			return false;
	}
	return true;
}

bool objbinsave(const char *path, const ObjData &data)
{
	FILE *pFile = boost::nowide::fopen(path, "wb");
	if (pFile == 0)
		return false;

	size_t version = 1;
	::fwrite(&version, 1, sizeof(version), pFile);

	bool result =
		savevector(pFile, data.coordinates)			&&
		savevector(pFile, data.textureCoordinates)	&&
		savevector(pFile, data.normals)				&&
		savevector(pFile, data.parameters)			&&
		savevector(pFile, data.mtllibs)				&&
		savevectornameidx(pFile, data.usemtls)		&&
		savevectornameidx(pFile, data.objects)		&&
		savevectornameidx(pFile, data.groups)		&&
		savevector(pFile, data.smoothingGroups)		&&
		savevector(pFile, data.vertices);

	::fclose(pFile);
	return result;
}

bool objbinload(const char *path, ObjData &data)
{
	FILE *pFile = boost::nowide::fopen(path, "rb");
	if (pFile == 0)
		return false;

	data.version = 0;
	if (::fread(&data.version, sizeof(data.version), 1, pFile) != 1)
		return false;
	if (data.version != 1)
		return false;

	bool result =
		loadvector(pFile, data.coordinates)			&&
		loadvector(pFile, data.textureCoordinates)	&&
		loadvector(pFile, data.normals)				&&
		loadvector(pFile, data.parameters)			&&
		loadvector(pFile, data.mtllibs)				&&
		loadvectornameidx(pFile, data.usemtls)		&&
		loadvectornameidx(pFile, data.objects)		&&
		loadvectornameidx(pFile, data.groups)		&&
		loadvector(pFile, data.smoothingGroups)		&&
		loadvector(pFile, data.vertices);

	::fclose(pFile);
	return result;
}

template<typename T>
bool vectorequal(const std::vector<T> &v1, const std::vector<T> &v2)
{
	if (v1.size() != v2.size())
		return false;
	for (size_t i = 0; i < v1.size(); ++ i)
		if (! (v1[i] == v2[i]))
			return false;
	return true;
}

bool vectorequal(const std::vector<std::string> &v1, const std::vector<std::string> &v2)
{
	if (v1.size() != v2.size())
		return false;
	for (size_t i = 0; i < v1.size(); ++ i)
		if (v1[i].compare(v2[i]) != 0)
			return false;
	return true;
}

extern bool objequal(const ObjData &data1, const ObjData &data2)
{
	//FIXME ignore version number
	// version;

	return 
		vectorequal(data1.coordinates,			data2.coordinates)			&&
		vectorequal(data1.textureCoordinates,	data2.textureCoordinates)	&&
		vectorequal(data1.normals,				data2.normals)				&&
		vectorequal(data1.parameters,			data2.parameters)			&&
		vectorequal(data1.mtllibs,				data2.mtllibs)				&&
		vectorequal(data1.usemtls,				data2.usemtls)				&&
		vectorequal(data1.objects,				data2.objects)				&&
		vectorequal(data1.groups,				data2.groups)				&&
		vectorequal(data1.vertices,				data2.vertices);
}

} // namespace ObjParser
