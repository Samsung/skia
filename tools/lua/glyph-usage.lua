function tostr(t)
    local str = ""
    for k, v in next, t do
        if #str > 0 then
            str = str .. ", "
        end
        if type(k) == "number" then
            str = str .. "[" .. k .. "] = "
        else
            str = str .. tostring(k) .. " = "
        end
        if type(v) == "table" then
            str = str .. "{ " .. tostr(v) .. " }"
        else
            str = str .. tostring(v)
        end
    end
    return str
end

local canvas        -- holds the current canvas (from startcanvas())

--[[
    startcanvas() is called at the start of each picture file, passing the
    canvas that we will be drawing into, and the name of the file.
    
    Following this call, there will be some number of calls to accumulate(t)
    where t is a table of parameters that were passed to that draw-op.
    
        t.verb is a string holding the name of the draw-op (e.g. "drawRect")
    
    when a given picture is done, we call endcanvas(canvas, fileName)
]]
function sk_scrape_startcanvas(c, fileName)
    canvas = c
end

--[[
    Called when the current canvas is done drawing.
]]
function sk_scrape_endcanvas(c, fileName)
    canvas = nil
end

--[[
    Called with the parameters to each canvas.draw call, where canvas is the
    current canvas as set by startcanvas()
]]

function round(x, mul)
    mul = mul or 1
    return math.floor(x * mul + 0.5) / mul
end

local strikes = {}  -- [fontID_pointsize] = [] unique glyphs

function make_strike_key(paint)
    return paint:getFontID() * 1000 + paint:getTextSize()
end

-- array is an array of bools (true), using glyphID as the index
-- other is just an array[1...N] of numbers (glyphIDs)
function array_union(array, other)
    for k, v in next, other do
        array[v] = true;
    end
end

function array_count(array)
    local n = 0
    for k in next, array do
        n = n + 1
    end
    return n
end

function sk_scrape_accumulate(t)
    verb = t.verb;
    if verb == "drawPosText" or verb == "drawPosTextH" then
        if t.glyphs then
            local key = make_strike_key(t.paint)
            strikes[key] = strikes[key] or {}
            array_union(strikes[key], t.glyphs)
        end
    end
end

--[[
    lua_pictures will call this function after all of the pictures have been
    "accumulated".
]]
function sk_scrape_summarize()
    local totalCount = 0
    local strikeCount = 0
    local min, max = 0, 0

    for k, v in next, strikes do
        local fontID = round(k / 1000)
        local size = k - fontID * 1000
        local count = array_count(v)

        io.write("fontID = ", fontID, ", size = ", size, ", entries = ", count, "\n");
        
        min = math.min(min, count)
        max = math.max(max, count)
        totalCount = totalCount + count
        strikeCount = strikeCount + 1
    end
    local ave = round(totalCount / strikeCount)

    io.write("\n", "unique glyphs: min = ", min, ", max = ", max, ", ave = ", ave, "\n");
end

