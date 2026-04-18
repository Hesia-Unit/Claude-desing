with Interfaces.C;
with Hesia_Sentinel_Core;

package body Hesia_Sentinel_Api is
   function Run return Interfaces.C.int is
   begin
      return Interfaces.C.int (Hesia_Sentinel_Core.Check);
   end Run;
end Hesia_Sentinel_Api;
