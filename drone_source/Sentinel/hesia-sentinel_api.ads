with Interfaces.C;

package Hesia_Sentinel_Api is
   function Run return Interfaces.C.int;
   pragma Export (C, Run, "hesia_sentinel_run");
end Hesia_Sentinel_Api;
