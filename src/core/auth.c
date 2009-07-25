/*
 * uhub - A tiny ADC p2p connection hub
 * Copyright (C) 2007-2009, Jan Vidar Krey
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "uhub.h"

#define ACL_ADD_USER(S, L, V) do { ret = check_cmd_user(S, V, L, line, line_count); if (ret != 0) return ret; } while(0)
#define ACL_ADD_BOOL(S, L)    do { ret = check_cmd_bool(S,    L, line, line_count); if (ret != 0) return ret; } while(0)
#define ACL_ADD_ADDR(S, L)    do { ret = check_cmd_addr(S,    L, line, line_count); if (ret != 0) return ret; } while(0)

const char* get_user_credential_string(enum user_credentials cred)
{
	switch (cred)
	{
		case cred_none:         return "none";
		case cred_bot:          return "bot";
		case cred_guest:        return "guest";
		case cred_user:         return "user";
		case cred_operator:     return "operator";
		case cred_super:        return "super";
		case cred_admin:        return "admin";
		case cred_link:         return "link";
	}
	
	return "";
};

static int check_cmd_bool(const char* cmd, struct linked_list* list, char* line, int line_count)
{
	char* data;
	char* data_extra;
	
	if (!strncmp(line, cmd, strlen(cmd)))
	{
		data = &line[strlen(cmd)];
		data_extra = 0;
		data[0] = '\0';
		data++;
		
		data = strip_white_space(data);
		if (!*data)
		{
			LOG_FATAL("ACL parse error on line %d", line_count);
			return -1;
		}
		
		list_append(list, hub_strdup(data));
		LOG_DEBUG("ACL: Deny access for: '%s' (%s)", data, cmd);
		return 1;
	}
	return 0;
}

static int check_cmd_user(const char* cmd, int status, struct linked_list* list, char* line, int line_count)
{
	char* data;
	char* data_extra;
	struct user_access_info* info = 0;
	
	if (!strncmp(line, cmd, strlen(cmd)))
	{
		data = &line[strlen(cmd)];
		data_extra = 0;
		data[0] = '\0';
		data++;
		
		data = strip_white_space(data);
		if (!*data)
		{
			LOG_FATAL("ACL parse error on line %d", line_count);
			return -1;
		}
		
		info = hub_malloc_zero(sizeof(struct user_access_info));
		
		if (!info)
		{
			LOG_ERROR("ACL parse error. Out of memory!");
			return -1;
		}
		
		if (strncmp(cmd, "user_", 5) == 0)
		{
			data_extra = strrchr(data, ':');
			if (data_extra)
			{
				data_extra[0] = 0;
				data_extra++;
			}
		}
		
		info->username = hub_strdup(data);
		info->password = data_extra ? hub_strdup(data_extra) : 0;
		info->status = status;
		list_append(list, info);
		LOG_DEBUG("ACL: Added user '%s' (%s)", info->username, get_user_credential_string(info->status));
		return 1;
	}
	return 0;
}


static void add_ip_range(struct linked_list* list, struct ip_ban_record* info)
{
	char buf1[INET6_ADDRSTRLEN+1];
	char buf2[INET6_ADDRSTRLEN+1];
	
	if (info->lo.af == AF_INET)
	{
		net_address_to_string(AF_INET, &info->lo.internal_ip_data.in.s_addr, buf1, INET6_ADDRSTRLEN);
		net_address_to_string(AF_INET, &info->hi.internal_ip_data.in.s_addr, buf2, INET6_ADDRSTRLEN);
	}
	else if (info->lo.af == AF_INET6)
	{
		net_address_to_string(AF_INET6, &info->lo.internal_ip_data.in6, buf1, INET6_ADDRSTRLEN);
		net_address_to_string(AF_INET6, &info->hi.internal_ip_data.in6, buf2, INET6_ADDRSTRLEN);
	}
	LOG_DEBUG("ACL: Deny access for: %s-%s", buf1, buf2);
	
	list_append(list, info);
}


static int check_ip_range(const char* lo, const char* hi, struct ip_ban_record* info)
{
	int ret1, ret2;
	
	if ((ip_is_valid_ipv4(lo) && ip_is_valid_ipv4(hi)) ||
		(ip_is_valid_ipv6(lo) && ip_is_valid_ipv6(hi)))
	{
		ret1 = ip_convert_to_binary(lo, &info->lo);
		ret2 = ip_convert_to_binary(hi, &info->hi);
		if (ret1 == -1 || ret2 == -1 || ret1 != ret2)
		{
			return -1;
		}
		return 0;
	}
	return -1;
}


static int check_ip_mask(const char* text_addr, int bits, struct ip_ban_record* info)
{
	LOG_DEBUG("ACL: Deny access for: %s/%d", text_addr, bits);
	
	if (ip_is_valid_ipv4(text_addr) ||
		ip_is_valid_ipv6(text_addr))
	{
		struct ip_addr_encap addr;
		struct ip_addr_encap mask1;
		struct ip_addr_encap mask2;
		int af = ip_convert_to_binary(text_addr, &addr);  /* 192.168.1.2 */
		int maxbits = af == AF_INET6 ? 128 : 32;
		ip_mask_create_left(af, bits, &mask1);            /* 255.255.255.0 */
		ip_mask_create_right(af, maxbits - bits, &mask2); /* 0.0.0.255 */
		ip_mask_apply_AND(&addr, &mask1, &info->lo);      /* 192.168.1.0 */
		ip_mask_apply_OR(&info->lo, &mask2, &info->hi);   /* 192.168.1.255 */
		return 0;
	}
	return -1;
}


static int check_cmd_addr(const char* cmd, struct linked_list* list, char* line, int line_count)
{
	char* data1;
	char* data2;
	struct ip_ban_record* info = 0;
	int cidr_bits = 0;
		
	if (!strncmp(line, cmd, strlen(cmd)))
	{
		data1 = &line[strlen(cmd)];
		data2 = 0;
		data1[0] = '\0';
		data1++;
		
		data1 = strip_white_space(data1);
		if (!*data1)
		{
			LOG_FATAL("ACL parse error on line %d", line_count);
			return -1;
		}
		
		info = hub_malloc_zero(sizeof(struct ip_ban_record));
		
		if (!info)
		{
			LOG_ERROR("ACL parse error. Out of memory!");
			return -1;
		}
		
		/* Extract IP-range */
		data2 = strrchr(data1, '-');
		if (data2)
		{
			cidr_bits = -1;
			data2[0] = 0;
			data2++;
			
			if (check_ip_range(data1, data2, info) == -1)
			{
				hub_free(info);
				return 0;
			}
			
			add_ip_range(list, info);

			return 1;
		}
		else
		{
			/* Extract IP-bitmask */
			data2 = strrchr(data1, '/');
			if (data2)
			{
				data2[0] = 0;
				data2++;
				cidr_bits = uhub_atoi(data2);
			}
			else
			{
				cidr_bits = 128;
			}
			
			if (check_ip_mask(data1, cidr_bits, info) == -1)
			{
				hub_free(info);
				return 0;
			}
			
			add_ip_range(list, info);
			
			return 1;
		}
	}
	return 0;
}



static int acl_parse_line(char* line, int line_count, void* ptr_data)
{
	char* pos;
	struct acl_handle* handle = (struct acl_handle*) ptr_data;
	int ret;
	
	if ((pos = strchr(line, '#')) != NULL)
	{
		pos[0] = 0;
	}
	
	if (!*line)
		return 0;

	LOG_DEBUG("acl_parse_line(): '%s'", line);
	line = strip_white_space(line);
	
	if (!*line)
	{
		LOG_FATAL("ACL parse error on line %d", line_count);
		return -1;
	}

	LOG_DEBUG("acl_parse_line: '%s'", line);
	
	ACL_ADD_USER("bot",        handle->users, cred_bot);
	ACL_ADD_USER("user_admin", handle->users, cred_admin);
	ACL_ADD_USER("user_super", handle->users, cred_super);
	ACL_ADD_USER("user_op",    handle->users, cred_operator);
	ACL_ADD_USER("user_reg",   handle->users, cred_user);
	ACL_ADD_USER("link",       handle->users, cred_link);
	ACL_ADD_BOOL("deny_nick",  handle->users_denied);
	ACL_ADD_BOOL("ban_nick",   handle->users_banned);
	ACL_ADD_BOOL("ban_cid",    handle->cids);
	ACL_ADD_ADDR("deny_ip",    handle->networks);
	ACL_ADD_ADDR("nat_ip",     handle->nat_override);
	
	LOG_ERROR("Unknown ACL command on line %d: '%s'", line_count, line);
	return -1;
}


int acl_initialize(struct hub_config* config, struct acl_handle* handle)
{
	int ret;
	memset(handle, 0, sizeof(struct acl_handle));
	
	handle->users        = list_create();
	handle->users_denied = list_create();
	handle->users_banned = list_create();
	handle->cids         = list_create();
	handle->networks     = list_create();
	handle->nat_override = list_create();
	
	if (!handle->users || !handle->cids || !handle->networks || !handle->users_denied || !handle->users_banned || !handle->nat_override)
	{
		LOG_FATAL("acl_initialize: Out of memory");
		
		list_destroy(handle->users);
		list_destroy(handle->users_denied);
		list_destroy(handle->users_banned);
		list_destroy(handle->cids);
		list_destroy(handle->networks);
		list_destroy(handle->nat_override);
		return -1;
	}
	
	if (config)
	{
		if (!*config->file_acl) return 0;
		
		ret = file_read_lines(config->file_acl, handle, &acl_parse_line);
		if (ret == -1)
			return -1;
	}
	return 0;
}


static void acl_free_access_info(void* ptr)
{
	struct user_access_info* info = (struct user_access_info*) ptr;
	if (info)
	{
		hub_free(info->username);
		hub_free(info->password);
		hub_free(info);
	}
}


static void acl_free_ip_info(void* ptr)
{
	struct access_info* info = (struct access_info*) ptr;
	if (info)
	{
		hub_free(info);
	}
}

int acl_shutdown(struct acl_handle* handle)
{
	if (handle->users)
	{
		list_clear(handle->users, &acl_free_access_info);
		list_destroy(handle->users);
	}
	
	if (handle->users_denied)
	{
		list_clear(handle->users_denied, &hub_free);
		list_destroy(handle->users_denied);
	}
	
	if (handle->users_banned)
	{
		list_clear(handle->users_banned, &hub_free);
		list_destroy(handle->users_banned);
	}
	
	
	if (handle->cids)
	{
		list_clear(handle->cids, &hub_free);
		list_destroy(handle->cids);
	}
	
	if (handle->networks)
	{
		list_clear(handle->networks, &acl_free_ip_info);
		list_destroy(handle->networks);
	}
	
	if (handle->nat_override)
	{
		list_clear(handle->nat_override, &acl_free_ip_info);
		list_destroy(handle->nat_override);
	}
	
	
	memset(handle, 0, sizeof(struct acl_handle));
	return 0;
}


struct user_access_info* acl_get_access_info(struct acl_handle* handle, const char* name)
{
	struct user_access_info* info = (struct user_access_info*) list_get_first(handle->users);
	while (info)
	{
		if (strcasecmp(info->username, name) == 0)
		{
			return info;
		}
		info = (struct user_access_info*) list_get_next(handle->users);
	}
	return NULL;
}

#define STR_LIST_CONTAINS(LIST, STR) \
		char* str = (char*) list_get_first(LIST); \
		while (str) \
		{ \
			if (strcasecmp(str, STR) == 0) \
				return 1; \
			str = (char*) list_get_next(LIST); \
		} \
		return 0

int acl_is_cid_banned(struct acl_handle* handle, const char* data)
{
	if (!handle) return 0;
	STR_LIST_CONTAINS(handle->cids, data);
}

int acl_is_user_banned(struct acl_handle* handle, const char* data)
{
	if (!handle) return 0;
	STR_LIST_CONTAINS(handle->users_banned, data);
}

int acl_is_user_denied(struct acl_handle* handle, const char* data)
{
	if (!handle) return 0;
	STR_LIST_CONTAINS(handle->users_denied, data);
}

int acl_user_ban_nick(struct acl_handle* handle, const char* nick)
{
	struct user_access_info* info = hub_malloc_zero(sizeof(struct user_access_info));
	if (!info)
	{
		LOG_ERROR("ACL error: Out of memory!");
		return -1;
	}
	list_append(handle->users_banned, hub_strdup(nick));
	return 0;
}

int acl_user_ban_cid(struct acl_handle* handle, const char* cid)
{
	struct user_access_info* info = hub_malloc_zero(sizeof(struct user_access_info));
	if (!info)
	{
		LOG_ERROR("ACL error: Out of memory!");
		return -1;
	}
	list_append(handle->cids, hub_strdup(cid));
	return 0;
}

int acl_user_unban_nick(struct acl_handle* handle, const char* nick)
{
	return -1;
}

int acl_user_unban_cid(struct acl_handle* handle, const char* cid)
{
	return -1;
}


int acl_is_ip_banned(struct acl_handle* handle, const char* ip_address)
{
	struct ip_addr_encap raw;
	struct ip_ban_record* info = (struct ip_ban_record*) list_get_first(handle->networks);
	ip_convert_to_binary(ip_address, &raw);
	
	while (info)
	{
		if (acl_check_ip_range(&raw, info))
		{
			return 1;
		}
		info = (struct ip_ban_record*) list_get_next(handle->networks);
	}
	return 0;
}

int acl_is_ip_nat_override(struct acl_handle* handle, const char* ip_address)
{
	struct ip_addr_encap raw;
	struct ip_ban_record* info = (struct ip_ban_record*) list_get_first(handle->nat_override);
	ip_convert_to_binary(ip_address, &raw);
	
	while (info)
	{
		if (acl_check_ip_range(&raw, info))
		{
			return 1;
		}
		info = (struct ip_ban_record*) list_get_next(handle->nat_override);
	}
	return 0;
}


int acl_check_ip_range(struct ip_addr_encap* addr, struct ip_ban_record* info)
{
	return (addr->af == info->lo.af && ip_compare(&info->lo, addr) <= 0 && ip_compare(addr, &info->hi) <= 0);
}

/*
 * This will generate the same challenge to the same user, always.
 * The challenge is made up of the time of the user connected
 * seconds since the unix epoch (modulus 1 million)
 * and the SID of the user (0-1 million).
 */
const char* acl_password_generate_challenge(struct acl_handle* acl, struct user* user)
{
	char buf[32];
	uint64_t tiger_res[3];
	static char tiger_buf[MAX_CID_LEN+1];
	
	snprintf(buf, 32, "%d%d%d", (int) user->net.tm_connected, (int) user->id.sid, (int) user->net.sd);
	
	tiger((uint64_t*) buf, strlen(buf), (uint64_t*) tiger_res);
	base32_encode((unsigned char*) tiger_res, TIGERSIZE, tiger_buf);
	tiger_buf[MAX_CID_LEN] = 0;
	return (const char*) tiger_buf;
}


int acl_password_verify(struct acl_handle* acl, struct user* user, const char* password)
{
	char buf[1024];
	struct user_access_info* access;
	const char* challenge;
	char raw_challenge[64];
	char password_calc[64];
	uint64_t tiger_res[3];
	
	if (!password || !user || strlen(password) != MAX_CID_LEN)
		return 0;

	access = acl_get_access_info(acl, user->id.nick);
	if (!access || !access->password)
		return 0;

	if (TIGERSIZE+strlen(access->password) >= 1024)
		return 0;

	challenge = acl_password_generate_challenge(acl, user);

	base32_decode(challenge, (unsigned char*) raw_challenge, MAX_CID_LEN);

	memcpy(&buf[0], (char*) access->password, strlen(access->password));
	memcpy(&buf[strlen(access->password)], raw_challenge, TIGERSIZE);
	
	tiger((uint64_t*) buf, TIGERSIZE+strlen(access->password), (uint64_t*) tiger_res);
	base32_encode((unsigned char*) tiger_res, TIGERSIZE, password_calc);
	password_calc[MAX_CID_LEN] = 0;

	if (strcasecmp(password, password_calc) == 0)
	{
		return 1;
	}
	return 0;
}



