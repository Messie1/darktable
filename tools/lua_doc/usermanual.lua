local function get_node_with_link(node,name)
		 if node:get_attribute("skiped") then return name end
		 return '<link linkend="'..node:get_name():gsub("%.","_"):gsub("#","_hash_")..'">'..name..'</link>'
end

para = function() return "</para>\n<para>" end
node_to_string = function(node,name) 
	if name then
		return get_node_with_link(node,name)
	else
		return get_node_with_link(node,node:get_name())
	end
end

code = function(text) 
	return "\n\n<programlisting language=\"lua\">"..text.."</programlisting>\n\n"
end

startlist = function() return "<itemizedlist>\n" end
endlist = function() return "</itemizedlist>\n" end
listel = function(text) return "<listitem><para>"..text.."</para></listitem>\n" end
emphasis = function(text) return "<emphasis>"..text.."</emphasis>" end


require "content"
doc = require "core"
table = require "table"
dump = require("darktable.debug").dump

local parse_doc_node

local function sorted_pairs (t, f)
	local a = {}
	for n in pairs(t) do table.insert(a, n) end
	table.sort(a, f)
	local i = 0      -- iterator variable
	local iter = function ()   -- iterator function
		i = i + 1
		if a[i] == nil then return nil
		else return a[i], t[a[i]]
		end
	end
	return iter
end


local function get_reported_type(node,simple)
	local rtype = doc.get_attribute(node,"reported_type")
	if not rtype then
		doc.debug_print(node)
		error("all types should have a reported type")
	end
	if rtype == "documentation node" then rtype = nil end
	if rtype == "dt_singleton" then rtype = nil end
	if( rtype and not simple and doc.get_attribute(node,"signature")) then
		rtype = rtype.."( "
		local sig = doc.get_attribute(node,"signature")
		for k,v in pairs(sig) do
			if(doc.get_attribute(v,"optional")) then
				rtype = rtype.."\n\t["..emphasis(get_node_with_link(v,doc.get_short_name(v))).." : "..get_reported_type(v,true).."]"
			else
				rtype = rtype.."\n\t"..emphasis(get_node_with_link(v,doc.get_short_name(v))).." : "..get_reported_type(v,true)
			end
			if next(sig,k) then
				rtype = rtype..", "
			end
		end
		rtype = rtype.."\n)"
	end
	if(not simple and doc.get_attribute(node,"ret_val")) then
		rtype = rtype.." : "..get_reported_type(doc.get_attribute(node,"ret_val",true))
	end
	return rtype
end

local function print_attributes(node)
	local result = ""
	for k2,v2 in sorted_pairs(node._luadoc_attributes) do
		if not doc.get_attribute(doc.toplevel.attributes[k2],"internal_attr") then
			if(type(v2) == "boolean") then
				result = result..listel(emphasis(get_node_with_link(doc.toplevel.attributes[k2],k2)))
			elseif type(v2) == "string" then
				result = result..listel(emphasis(get_node_with_link(doc.toplevel.attributes[k2],k2).." : ")..v2)
			elseif type(v2) == "table" and v2._luadoc_type then
				result = result..listel(emphasis(get_node_with_link(doc.toplevel.attributes[k2],k2).." : ")..tostring(v2))
			elseif type(v2) == "table" then
				tmp = ""
				for k,v in pairs(v2) do
					tmp = tmp..listel(tostring(v));
				end
				result = result..listel(emphasis(get_node_with_link(doc.toplevel.attributes[k2],k2).." : ")..para()..startlist()..tmp..endlist())
			elseif type(v2) == "number" then
				result = result..listel(emphasis(get_node_with_link(doc.toplevel.attributes[k2],k2).." : ")..tostring(v2))
			else
				error("unhandle attribute type\n"..dump(v2,k2))
			end
		end
	end
	if  result ~= "" then
		-- synopsis was within the <entry> below
		result = [[<informaltable frame="none" width="80%"><tgroup cols="2" colsep="0" rowsep="0">
<colspec colwidth="2*"/>
<colspec colwidth="8*"/>
<tbody><row>
<entry>Attributes:</entry>
<entry>]]..startlist()..result..endlist()..[[</entry>
</row></tbody>
</tgroup></informaltable>]]
	end

	return result

end


local function print_content(node)
	local rtype = get_reported_type(node)
	local result = ""
	if  rtype then
		-- synopsis was here
		result = result .."<synopsis>"..rtype.."</synopsis>\n"
	end
	result = result .."<para>"..doc.get_text(node).."</para>\n"
	result = result ..print_attributes(node)
	result = result.."\n"
	local sig = doc.get_attribute(node,"signature")
	local ret_val = doc.get_attribute(node,"ret_val")
	if(sig or ret_val) then
		result = result.."<variablelist>\n"
	end
	if(sig) then
		for k,v in pairs(sig) do
			result = result .. parse_doc_node(v,node,doc.get_short_name(v)).."\n";
		end
	end
	if(ret_val) then
		result = result .. parse_doc_node(ret_val,node,doc.get_short_name(ret_val)).."\n";
		result = result.."\n"
	end
	if(sig or ret_val) then
		result = result.."</variablelist>\n"
	end

	for k,v in doc.unskiped_children(node) do
		result = result .. parse_doc_node(v,node,k).."\n";
	end
	return result;
end

local function depth(node)
	if doc.get_name(node) == "" then return 0 end
	return depth(doc.get_main_parent(node)) +1
end

parse_doc_node = function(node,parent,prev_name)
	local node_name
	local parent_name = doc.get_name(parent)
	local result = ""
	if doc.is_main_parent(node,parent,prev_name) then
		node_name = doc.get_name(node)
	else
		if parent_name == "" then
			node_name = prev_name
		else 
			node_name =  doc.get_name(parent).."."..prev_name
		end
	end

	local depth = depth(node);
	--if depth > 5 then 
	--	error("too many tree depth for usermanual")
	--end

	if(node._luadoc_type == "param") then
		local tmp_node = doc.get_main_parent(node)
		while doc.get_attribute(tmp_node,"reported_type") == "function" do
			tmp_node = doc.get_main_parent(tmp_node)
		end
		if(node:get_short_name() == "return") then
			result = result ..'<varlistentry><term><emphasis>return</emphasis></term><listitem>\n'
		else
			result = result ..'<varlistentry id="'..doc.get_name(node):gsub("%.","_"):gsub("#","_hash_")..'"><term>'..doc.get_short_name(node).."</term><listitem>\n"
		end
	elseif depth ~= 0 then
		--result = result..'<sect'..(depth+1)..' status="final" '
		result = result..'<section status="final" '
		if(doc.is_main_parent(node,parent,prev_name) ) then
			result = result..'id="'..doc.get_name(node):gsub("%.","_"):gsub("#","_hash_")..'"'
		end
		result = result..'>\n'
		result = result..'<title>'..node_name..'</title>\n'
		result = result..'<indexterm>\n'
		result = result..'<primary>Lua API</primary>\n'
		result = result..'<secondary>'..prev_name..'</secondary>\n'
		result = result..'</indexterm>\n'
	end
	if(not doc.is_main_parent(node,parent,prev_name) ) then
		result = result .. "<para>see "..get_node_with_link(node,doc.get_name(node)).."</para>\n"
	else
		result = result .. print_content(node,parent)
	end
	if(node._luadoc_type == "param") then
		result = result..'</listitem></varlistentry>\n'
	elseif depth ~= 0 then
		--result = result..'</sect'..(depth+1)..'>\n'
		result = result..'</section>\n'
	end
	return result;
end


M = {}

M.page_name = page_name

function M.get_doc()
	 return [[<!DOCTYPE section PUBLIC "-//OASIS//DTD DocBook XML V4.5//EN"
               "http://www.oasis-open.org/docbook/xml/4.5/docbookx.dtd" [
		<!ENTITY % darktable_dtd SYSTEM "../dtd/darktable.dtd">
		%darktable_dtd;
		]>
   <section status="draft" id="lua_api"><title>Lua API</title>
   <indexterm>
      <primary>Lua API</primary>
   </indexterm>
]]..parse_doc_node(doc.toplevel,nil,"").."</section>"


end


return M;
